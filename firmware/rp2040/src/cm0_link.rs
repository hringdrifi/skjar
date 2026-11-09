//! CM0 との UART のやり取り（docs/protocol.md の 1 章）。
//!
//! 1 行 1 メッセージ：`$SK,<コマンド>[,<引数>...]*<CRC-8 の 16 進 2 桁>\n`
//! CM0 の起動中のログ（コンソール）も同じ線に流れてくるので、この形でない行は捨てる。
//!
//! いまできること：
//! - CM0 → RP2040：`HELLO`、`ALIVE`、`TK`（画面のタッチキー。キーマップの 7〜10 行目として RMK に渡す）、
//!   `KEY`（Media タブなどの固定のキー）、`PTR` / `WHEEL`（トラックパッド）、`LEDF`（キーのライトの 1 コマ）、
//!   `OUT`、`LCD`（記録だけ）、`UF2`
//! - RP2040 → CM0：`ALIVE`、`BLMAX`（いまは 255 固定）、`OUT`、`LAYER`、`TKL`（タッチキーの割り当て）、
//!   `USER`（Vial の USER キー。UI がタブを開く）、`ACT`（文字や機能のキーが押された）
//! - `PWR` は CC の判定を作るまで送らない。`SHUTDOWN` を出すきっかけもまだない（[`request_shutdown`]）

use core::cell::Cell;
use core::fmt::Write as _;

use embassy_futures::join::join4;
use embassy_futures::select::{Either, select};
use embassy_rp::uart::{BufferedUart, BufferedUartRx, BufferedUartTx};
use embassy_sync::blocking_mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_time::{Duration, Instant, Ticker};
use embedded_io_async::{Read, Write};
use log::{debug, info, warn};
use rmk::event::{ActionEvent, EventSubscriber, KeyboardEvent, LayerChangeEvent, SubscribableEvent, publish_event};
use rmk::heapless::String;
use rmk::keymap::KeyMap;
use rmk::types::action::{Action, KeyAction};
use rmk::types::keycode::KeyCode;

use crate::keymap::{NUM_LAYER, TOUCH_KEYS, touch_key_pos};
use crate::{reports, rgb};

const LINE_MAX: usize = 64;
const ALIVE_PERIOD: Duration = Duration::from_secs(1);
const LINK_TIMEOUT: Duration = Duration::from_secs(3);

/// 物理キーの出力先の選び方（CM0 の `OUT` で変わる）。
#[derive(Clone, Copy, PartialEq, Eq)]
enum OutMode {
    Auto,
    Pc,
    Cm0,
}

/// CM0 に送るもの。
#[derive(Clone, Copy)]
enum Msg {
    State,
    Out,
    Layer,
    Shutdown,
    /// USER キーを押した（true）・離した（false）
    User(u8, bool),
    /// 文字や機能のキーが押された
    Act,
}

static TX_QUEUE: Channel<CriticalSectionRawMutex, Msg, 16> = Channel::new();

struct Shared {
    out_mode: Cell<OutMode>,
    layer: Cell<u8>,
    /// CM0 から最後に正しい行を受けた時刻。まだなら None
    last_rx: Cell<Option<Instant>>,
    /// 押されているタッチキー（ビット i がタッチキー i）
    touch_pressed: Cell<u32>,
    /// `ACT` を送る待ちに入っている（続けて押されても 1 回にまとめる）
    act_pending: Cell<bool>,
}

static SHARED: Mutex<CriticalSectionRawMutex, Shared> = Mutex::new(Shared {
    out_mode: Cell::new(OutMode::Auto),
    layer: Cell::new(0),
    last_rx: Cell::new(None),
    touch_pressed: Cell::new(0),
    act_pending: Cell::new(false),
});

fn send(msg: Msg) {
    if TX_QUEUE.try_send(msg).is_err() {
        warn!("cm0_link: tx queue full");
    }
}

/// CM0 にシャットダウンを求める（電源の判定から呼ぶ予定）。
#[allow(dead_code)]
pub fn request_shutdown() {
    send(Msg::Shutdown);
}

/// CRC-8（多項式 0x07、初期値 0x00、反転なし）。
pub fn crc8(data: &[u8]) -> u8 {
    let mut crc = 0u8;
    for &b in data {
        crc ^= b;
        for _ in 0..8 {
            crc = if crc & 0x80 != 0 { (crc << 1) ^ 0x07 } else { crc << 1 };
        }
    }
    crc
}

/// UART を動かし続ける。keymap はタッチキーの割り当てを CM0 に知らせるのに使う。
pub async fn run(uart: BufferedUart, keymap: &KeyMap<'_>) -> ! {
    let (tx, rx) = uart.split();
    info!("cm0_link: started");
    join4(rx_loop(rx), tx_loop(tx, keymap), layer_loop(), action_loop()).await;
    unreachable!()
}

// ---- タッチキー ----

fn touch_key(index: usize, pressed: bool) {
    if index >= TOUCH_KEYS {
        return;
    }
    let bit = 1u32 << index;
    let changed = SHARED.lock(|s| {
        let before = s.touch_pressed.get();
        let after = if pressed { before | bit } else { before & !bit };
        s.touch_pressed.set(after);
        before != after
    });
    if changed {
        let (row, col) = touch_key_pos(index);
        publish_event(KeyboardEvent::key(row, col, pressed));
    }
}

/// 押されたままのタッチキーをすべて離す（UI の起動、UART の途絶）。
fn release_touch_keys() {
    let pressed = SHARED.lock(|s| s.touch_pressed.get());
    for i in 0..TOUCH_KEYS {
        if pressed & (1 << i) != 0 {
            touch_key(i, false);
        }
    }
}

// ---- 受信 ----

async fn rx_loop(mut rx: BufferedUartRx) {
    let mut line = [0u8; LINE_MAX];
    let mut len = 0usize;
    let mut overflow = false;
    let mut buf = [0u8; 32];
    loop {
        let n = match rx.read(&mut buf).await {
            Ok(n) => n,
            Err(e) => {
                // ノイズや、CM0 の電源が切れたときの線の変化。捨てて続ける
                debug!("cm0_link: rx error {:?}", e);
                len = 0;
                overflow = false;
                continue;
            }
        };
        for &c in &buf[..n] {
            match c {
                b'\r' => {}
                b'\n' => {
                    if !overflow {
                        handle_line(&line[..len]);
                    }
                    len = 0;
                    overflow = false;
                }
                _ if len < LINE_MAX => {
                    line[len] = c;
                    len += 1;
                }
                _ => overflow = true,
            }
        }
    }
}

fn hex_digit(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'A'..=b'F' => Some(c - b'A' + 10),
        _ => None,
    }
}

fn parse_hex(s: &str) -> Option<u16> {
    if s.is_empty() || s.len() > 4 {
        return None;
    }
    u16::from_str_radix(s, 16).ok()
}

fn parse_bool(s: &str) -> Option<bool> {
    match s {
        "1" => Some(true),
        "0" => Some(false),
        _ => None,
    }
}

/// 1 行を読む。正しい行なら中身（`SK,` の後ろ）を返す。
fn parse_line(line: &[u8]) -> Option<&str> {
    // 行の途中にログが入っても拾えるよう、最後の "$SK," から読む
    let start = line.windows(4).rposition(|w| w == b"$SK,")?;
    let frame = &line[start..];
    let star = frame.iter().rposition(|&c| c == b'*')?;
    if frame.len() != star + 3 {
        return None;
    }
    let crc = hex_digit(frame[star + 1])? << 4 | hex_digit(frame[star + 2])?;
    if crc8(&frame[1..star]) != crc {
        return None;
    }
    core::str::from_utf8(&frame[4..star]).ok()
}

/// −127〜127 の 10 進数
fn parse_rel(s: &str) -> Option<i8> {
    let v: i16 = s.parse().ok()?;
    (-127..=127).contains(&v).then_some(v as i8)
}

fn handle_line(line: &[u8]) {
    let Some(body) = parse_line(line) else {
        return;
    };
    SHARED.lock(|s| s.last_rx.set(Some(Instant::now())));

    let mut f = body.split(',');
    let cmd = f.next().unwrap_or("");
    let args: [&str; 3] = [f.next().unwrap_or(""), f.next().unwrap_or(""), f.next().unwrap_or("")];
    match cmd {
        "ALIVE" => {}
        "HELLO" => {
            info!("cm0_link: HELLO");
            reports::release_touch();
            release_touch_keys();
            send(Msg::State);
        }
        "TK" => {
            let (Ok(index), Some(pressed)) = (args[0].parse::<usize>(), parse_bool(args[1])) else {
                warn!("cm0_link: bad TK {}", body);
                return;
            };
            touch_key(index, pressed);
        }
        "KEY" => {
            let (Some(page), Some(usage), Some(down)) = (parse_hex(args[0]), parse_hex(args[1]), parse_bool(args[2])) else {
                warn!("cm0_link: bad KEY {}", body);
                return;
            };
            reports::touch_key(page as u8, usage, down);
        }
        "LEDF" => {
            // キーのライトの 1 コマ（16 進 42 文字。1 文字が 1 キーの明るさ 0〜15、マトリクスの順）
            let hex = args[0].as_bytes();
            if hex.len() != rgb::LEDS {
                warn!("cm0_link: bad LEDF length {}", hex.len());
                return;
            }
            let mut levels = [0u8; rgb::LEDS];
            for (l, &c) in levels.iter_mut().zip(hex) {
                let Some(v) = hex_digit(c) else {
                    return;
                };
                *l = v;
            }
            rgb::show_frame(levels);
        }
        "PTR" => {
            let (Some(dx), Some(dy), Ok(buttons)) = (parse_rel(args[0]), parse_rel(args[1]), args[2].parse::<u8>()) else {
                warn!("cm0_link: bad PTR {}", body);
                return;
            };
            reports::touch_pointer(dx, dy, buttons);
        }
        "WHEEL" => {
            let (Some(v), Some(h)) = (parse_rel(args[0]), parse_rel(args[1])) else {
                warn!("cm0_link: bad WHEEL {}", body);
                return;
            };
            reports::touch_wheel(v, h);
        }
        "OUT" => {
            let mode = match args[0] {
                "AUTO" => OutMode::Auto,
                "PC" => OutMode::Pc,
                "CM0" => OutMode::Cm0,
                _ => return,
            };
            if mode == OutMode::Pc {
                // PC への USB HID はまだないので、出力先は CM0 のまま
                warn!("cm0_link: output to PC is not implemented yet");
            }
            SHARED.lock(|s| s.out_mode.set(mode));
            send(Msg::Out);
        }
        "LCD" => info!("cm0_link: LCD {} requested (not wired yet)", args[0]),
        "UF2" => {
            info!("cm0_link: rebooting into the UF2 bootloader");
            embassy_rp::rom_data::reset_to_usb_boot(0, 0);
        }
        _ => debug!("cm0_link: ignoring {}", cmd),
    }
}

// ---- 送信 ----

async fn write_body(tx: &mut BufferedUartTx, body: &str) {
    let mut line: String<{ LINE_MAX + 1 }> = String::new();
    let payload_len = 3 + body.len();
    if payload_len + 5 > LINE_MAX {
        warn!("cm0_link: message too long: {}", body);
        return;
    }
    let _ = write!(line, "$SK,{}", body);
    let crc = crc8(&line.as_bytes()[1..1 + payload_len]);
    let _ = writeln!(line, "*{:02X}", crc);
    if let Err(e) = tx.write_all(line.as_bytes()).await {
        warn!("cm0_link: tx error {:?}", e);
    }
}

async fn write_msg(tx: &mut BufferedUartTx, keymap: &KeyMap<'_>, msg: Msg) {
    let mut body: String<48> = String::new();
    match msg {
        Msg::State => {
            // PWR は CC の判定ができてから送る
            write_body(tx, "BLMAX,255").await;
            write_msg_out(tx).await;
            write_touch_map(tx, keymap).await;
            write_msg_layer(tx).await;
            return;
        }
        Msg::Out => {
            write_msg_out(tx).await;
            return;
        }
        Msg::Layer => {
            write_msg_layer(tx).await;
            return;
        }
        Msg::Shutdown => {
            let _ = body.push_str("SHUTDOWN");
        }
        Msg::User(id, pressed) => {
            let _ = write!(body, "USER,{},{}", id, pressed as u8);
        }
        Msg::Act => {
            SHARED.lock(|s| s.act_pending.set(false));
            let _ = body.push_str("ACT");
        }
    }
    write_body(tx, &body).await;
}

async fn write_msg_out(tx: &mut BufferedUartTx) {
    let mode = SHARED.lock(|s| s.out_mode.get());
    // 出力先はいまは CM0 だけ。手動で選ばれているかどうかだけを返す
    let body = if mode == OutMode::Auto { "OUT,CM0,AUTO" } else { "OUT,CM0,MAN" };
    write_body(tx, body).await;
}

async fn write_msg_layer(tx: &mut BufferedUartTx) {
    let layer = SHARED.lock(|s| s.layer.get());
    let mut body: String<16> = String::new();
    let _ = write!(body, "LAYER,{}", layer);
    write_body(tx, &body).await;
}

/// タッチキーの割り当てを、すべてのレイヤーの分送る（`TKL,<layer>,<i>,<code>`）。
/// Vial でキーマップを変えたあとも、これを送り直せば画面が追いつく。
async fn write_touch_map(tx: &mut BufferedUartTx, keymap: &KeyMap<'_>) {
    for layer in 0..NUM_LAYER {
        for i in 0..TOUCH_KEYS {
            let (row, col) = touch_key_pos(i);
            let action = keymap.action_at_pos(layer, row, col);
            let mut body: String<48> = String::new();
            let _ = write!(body, "TKL,{},{},", layer, i);
            push_key_action(&mut body, action);
            write_body(tx, &body).await;
        }
    }
}

/// キーの動作を、UI がラベルにできる短い文字列にする（docs/protocol.md の 1.4）。
fn push_key_action<const N: usize>(s: &mut String<N>, a: KeyAction) {
    match a {
        KeyAction::No => {
            let _ = s.push('-');
        }
        KeyAction::Transparent => {
            let _ = s.push('T');
        }
        KeyAction::Single(a) | KeyAction::Tap(a) => push_action(s, a),
        KeyAction::TapHold(tap, hold, _) => {
            push_action(s, tap);
            let _ = s.push('/');
            push_action(s, hold);
        }
        _ => {
            // Morse（タップダンス）や、RMK が今後足す種類
            let _ = s.push('?');
        }
    }
}

fn push_action<const N: usize>(s: &mut String<N>, a: Action) {
    let _ = match a {
        Action::No => write!(s, "-"),
        Action::Key(KeyCode::Hid(h)) => write!(s, "K{:02X}", h as u8),
        Action::Key(KeyCode::Consumer(c)) => write!(s, "C{:X}", u16::from(c)),
        Action::Key(KeyCode::SystemControl(c)) => write!(s, "S{:X}", c as u8),
        Action::KeyWithModifier(h, m) => write!(s, "K{:02X}+{:02X}", h as u8, m.into_bits()),
        Action::OneShotKey(h) => write!(s, "K{:02X}", h as u8),
        Action::Modifier(m) | Action::OneShotModifier(m) => write!(s, "M{:02X}", m.into_bits()),
        Action::LayerOn(n) | Action::LayerOnWithModifier(n, _) => write!(s, "L{}", n),
        Action::LayerToggle(n) | Action::LayerToggleOnly(n) => write!(s, "G{}", n),
        Action::DefaultLayer(n) | Action::PersistentDefaultLayer(n) => write!(s, "D{}", n),
        Action::OneShotLayer(n) => write!(s, "O{}", n),
        Action::User(n) => write!(s, "U{}", n),
        _ => write!(s, "?"),
    };
}

async fn tx_loop(mut tx: BufferedUartTx, keymap: &KeyMap<'_>) {
    let mut ticker = Ticker::every(ALIVE_PERIOD);
    let mut seq: u8 = 0;
    loop {
        match select(ticker.next(), TX_QUEUE.receive()).await {
            Either::First(()) => {
                let mut body: String<16> = String::new();
                let _ = write!(body, "ALIVE,{}", seq);
                seq = seq.wrapping_add(1);
                write_body(&mut tx, &body).await;
                check_timeout();
            }
            Either::Second(msg) => write_msg(&mut tx, keymap, msg).await,
        }
    }
}

/// CM0 から 3 秒届かなければ、押されたままのタッチキーとトラックパッドのボタンを離す。
fn check_timeout() {
    let expired = SHARED.lock(|s| match s.last_rx.get() {
        Some(t) if t.elapsed() >= LINK_TIMEOUT => {
            s.last_rx.set(None);
            true
        }
        _ => false,
    });
    if expired {
        info!("cm0_link: CM0 went quiet");
        reports::release_touch();
        release_touch_keys();
    }
}

// ---- レイヤー ----

async fn layer_loop() {
    // レイヤーの通知を受けられる数は keyboard.toml の [event.layer_change] subs で決まる。
    // 足りないと起動時に panic するので、受け取る場所を足すときはそちらも増やす。
    let mut sub = LayerChangeEvent::subscriber();
    loop {
        let LayerChangeEvent(layer) = sub.next_event().await;
        SHARED.lock(|s| s.layer.set(layer));
        send(Msg::Layer);
    }
}

// ---- キーの動作（USER キーと、元のタブに戻すきっかけ） ----

/// 元のタブに戻すきっかけになるキーか。文字や機能のキーだけで、修飾キー、レイヤーキー、
/// USER キー、メディアキー、マウスキーは数えない。
fn counts_as_typing(a: &Action) -> bool {
    match a {
        Action::Key(KeyCode::Hid(h)) => KeyCode::Hid(*h).is_basic_keyboard_key() && !h.is_modifier(),
        Action::KeyWithModifier(..) | Action::OneShotKey(_) => true,
        _ => false,
    }
}

async fn action_loop() {
    // ActionEvent の受け取り口は keyboard.toml の [event.action] subs で決まる（初期値は 0）。
    // RMK はキーの動作が決まるたびにこれを出し、受け取り口が埋まっていると待つので、すぐに返す。
    let mut sub = ActionEvent::subscriber();
    loop {
        let ev = sub.next_event().await;
        let pressed = ev.keyboard_event.pressed;
        match ev.action {
            Action::User(id) => send(Msg::User(id, pressed)),
            ref a if pressed && counts_as_typing(a) => {
                let first = SHARED.lock(|s| !s.act_pending.replace(true));
                if first {
                    send(Msg::Act);
                }
            }
            _ => {}
        }
    }
}
