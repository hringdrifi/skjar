//! CM0 との UART のやり取り（docs/protocol.md の 1 章）。
//!
//! 1 行 1 メッセージ：`$SK,<コマンド>[,<引数>...]*<CRC-8 の 16 進 2 桁>\n`
//! CM0 の起動中のログ（コンソール）も同じ線に流れてくるので、この形でない行は捨てる。
//!
//! いまできること：
//! - CM0 → RP2040：`HELLO`、`ALIVE`、`BYE`、`TK`（画面のタッチキー。キーマップの 7〜10 行目として RMK に渡す）、
//!   `KEY`（Media タブなどの固定のキー）、`PTR` / `WHEEL`（トラックパッド）、`LEDF`（キーのライトの 1 コマ）、
//!   `LCD`（記録だけ）、`UF2`
//! - RP2040 → CM0：`ALIVE`、`BLMAX`（いまは 255 固定）、`USB`（PC につながっているか）、`OUT`、`LAYER`、
//!   `TKL`（タッチキーの割り当て）、
//!   `USER`（Vial の USER キー。UI がタブを開く）、`ACT`（文字や機能のキーが押された）
//! - `PWR` は CC の判定を作るまで送らない。シャットダウンの依頼は、UART ではなく I²C の電源キー
//!   （[`request_shutdown`]。UI がいなくても効く）
//!
//! キーとトラックパッドの送り先（docs/protocol.md の 2.6）：
//! - UI が出ている（`HELLO` か `ALIVE` が来ていて、`BYE` も途切れもない）：PC があれば USB、なければ I²C
//! - UI がいない（`BYE` を受けた、`ALIVE` が [`UI_TIMEOUT`] 途切れた）：CM0 のカーネルが生きていれば
//!   I²C（コンソール）。生きていなければ（電源 OFF・起動中・止まる途中・固まった）、PC があれば USB
//! - カーネルが生きているかは、UI がいない間だけ、I²C に今と同じ内容のレポートを置いて、読みに来るかで見る
//! - キーマップの既定のレイヤーは、送り先に合わせる（USB ならキーボードモードの 0、I²C ならサイバーデックの 3）

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

use crate::keymap::{MODE_CYBERDECK_COL, MODE_KEYBOARD_COL, MODE_ROW, NUM_LAYER, TOUCH_KEYS, touch_key_pos};
use crate::{hid_i2c, reports, rgb};

const LINE_MAX: usize = 64;
/// RP2040 が `ALIVE` を送る間隔
const ALIVE_PERIOD: Duration = Duration::from_secs(1);
/// 送り先を見直す間隔（`ALIVE` の途切れ、カーネルの確かめ）
const SUPERVISE_PERIOD: Duration = Duration::from_millis(100);
/// CM0 から何も届かなくなって、押されたままのタッチキーとボタンを離すまで
const TOUCH_TIMEOUT: Duration = Duration::from_secs(3);
/// UI の `ALIVE` が途切れて、UI がいないとみなすまで。UI が落ちて systemd が起動し直す
/// （`RestartSec=1` ＋ UI の起動）より長くする。Zero 2 W（CM0 と同じ SoC）で、UI の起動は約 0.9 秒
/// （うち背景の絵が 0.7 秒）、落ちてから戻るまで約 2 秒だった（2026-11-12）
const UI_TIMEOUT: Duration = Duration::from_secs(3);
/// UI がいない間に、カーネルの確かめのレポートを置く間隔
const PROBE_PERIOD: Duration = Duration::from_secs(2);
/// これだけ読みに来なければ、カーネルは生きていないとみなす
const KERNEL_TIMEOUT: Duration = Duration::from_secs(6);

/// CM0 に送るもの。
#[derive(Clone, Copy)]
enum Msg {
    State,
    Out,
    Layer,
    /// USER キーを押した（true）・離した（false）
    User(u8, bool),
    /// 文字や機能のキーが押された
    Act,
    /// PC につながっているか（USB）
    Usb,
}

static TX_QUEUE: Channel<CriticalSectionRawMutex, Msg, 16> = Channel::new();

struct Shared {
    /// PC の USB に認識されている（configured）、スリープしている（suspended）
    usb_configured: Cell<bool>,
    usb_suspended: Cell<bool>,
    /// いまの送り先（true：PC の USB、false：CM0 の I²C）。キーマップのレイヤーもこれに合わせる
    to_pc: Cell<bool>,
    /// UI が出ている（`HELLO` か `ALIVE` を受けて、まだ `BYE` も途切れもない）
    ui_active: Cell<bool>,
    /// UI の `HELLO` / `ALIVE` を最後に受けた時刻
    last_alive: Cell<Option<Instant>>,
    /// UI がいなくなって、カーネルを確かめ始めた時刻（UI がいる間は None）
    probing_since: Cell<Option<Instant>>,
    /// カーネルが固まったと、もう知らせた
    hung_reported: Cell<bool>,
    layer: Cell<u8>,
    /// CM0 から最後に正しい行を受けた時刻。まだなら None
    last_rx: Cell<Option<Instant>>,
    /// 押されているタッチキー（ビット i がタッチキー i）
    touch_pressed: Cell<u32>,
    /// `ACT` を送る待ちに入っている（続けて押されても 1 回にまとめる）
    act_pending: Cell<bool>,
}

static SHARED: Mutex<CriticalSectionRawMutex, Shared> = Mutex::new(Shared {
    usb_configured: Cell::new(false),
    usb_suspended: Cell::new(false),
    // 初めは PC（キーボードモード）。すぐ update_output が今の状態に合わせる
    to_pc: Cell::new(true),
    ui_active: Cell::new(false),
    last_alive: Cell::new(None),
    probing_since: Cell::new(None),
    hung_reported: Cell::new(false),
    layer: Cell::new(0),
    last_rx: Cell::new(None),
    touch_pressed: Cell::new(0),
    act_pending: Cell::new(false),
});

/// キーとトラックパッドを PC（USB）に出すか（reports.rs）
pub fn output_to_pc() -> bool {
    SHARED.lock(|s| s.to_pc.get())
}

/// CM0 のカーネルが生きているか：止まる途中ではなく、最近 I²C を読みに来た
fn kernel_alive() -> bool {
    if hid_i2c::sleeping() {
        return false;
    }
    matches!(hid_i2c::last_read(), Some(t) if t.elapsed() < KERNEL_TIMEOUT)
}

/// 今あるべき送り先（モジュールの先頭の決まり）
fn desired_to_pc() -> bool {
    let pc = pc_connected();
    if SHARED.lock(|s| s.ui_active.get()) {
        pc
    } else if kernel_alive() {
        false
    } else {
        pc
    }
}

/// 送り先を、あるべき送り先に合わせる。変わったら、キーマップのレイヤーと出力先を切り替え、UI に知らせる
fn update_output() {
    let to_pc = desired_to_pc();
    let changed = SHARED.lock(|s| s.to_pc.replace(to_pc) != to_pc);
    if !changed {
        return;
    }
    info!("cm0_link: output to {}", if to_pc { "the PC (USB, keyboard mode)" } else { "CM0 (I2C, cyberdeck mode)" });
    // キーマップの「既定のレイヤー」を、キーボードモードは 0、サイバーデックモードは 3 にする。
    // キーマップの 11 行目にある df!(0) / df!(3) のキーを押したことにする（keymap.rs）
    let col = if to_pc { MODE_KEYBOARD_COL } else { MODE_CYBERDECK_COL };
    publish_event(KeyboardEvent::key(MODE_ROW, col, true));
    publish_event(KeyboardEvent::key(MODE_ROW, col, false));
    // キーとトラックパッドの出力先も切り替える（押したままのものは、元の出力先で離す）
    reports::retarget();
    send(Msg::Out);
}

/// UI が出た（`HELLO` / `ALIVE`）
fn ui_seen() {
    SHARED.lock(|s| {
        s.ui_active.set(true);
        s.last_alive.set(Some(Instant::now()));
        s.probing_since.set(None);
        s.hung_reported.set(false);
    });
    update_output();
}

/// UI がいなくなった（`BYE`、`ALIVE` の途切れ）。カーネルを確かめ始める
fn ui_gone(why: &str) {
    let was = SHARED.lock(|s| {
        let was = s.ui_active.replace(false);
        s.probing_since.set(Some(Instant::now()));
        was
    });
    if was {
        info!("cm0_link: UI gone ({})", why);
    }
    reports::release_touch();
    release_touch_keys();
    // すぐ確かめる。読みに来たら（数 ms）、次の見直しで I²C に切り替わる
    reports::probe_i2c();
    update_output();
}

/// 定期の見直し：UI の `ALIVE` の途切れ、タッチキーの離し忘れ、カーネルの確かめ
fn supervise() {
    let now = Instant::now();
    let (ui_active, last_alive, last_rx) = SHARED.lock(|s| (s.ui_active.get(), s.last_alive.get(), s.last_rx.get()));

    // CM0 から何も届かない：押されたままのタッチキーとボタンを離す
    if matches!(last_rx, Some(t) if now - t >= TOUCH_TIMEOUT) {
        SHARED.lock(|s| s.last_rx.set(None));
        reports::release_touch();
        release_touch_keys();
    }

    if ui_active {
        if matches!(last_alive, Some(t) if now - t >= UI_TIMEOUT) {
            ui_gone("no ALIVE");
        }
        return;
    }

    // UI がいない：カーネルが生きているかを確かめる
    static LAST_PROBE: Mutex<CriticalSectionRawMutex, Cell<Option<Instant>>> = Mutex::new(Cell::new(None));
    let due = LAST_PROBE.lock(|c| match c.get() {
        Some(t) if now - t < PROBE_PERIOD => false,
        _ => {
            c.set(Some(now));
            true
        }
    });
    if due {
        reports::probe_i2c();
    }

    // 固まった：確かめ始めてから KERNEL_TIMEOUT の間、一度も読みに来ない（止まる途中ではない）。
    // 本番では CM0 の電源を入れ直す（電源の判定と一緒に作る）。試作はログに出すだけ
    let (since, reported) = SHARED.lock(|s| (s.probing_since.get(), s.hung_reported.get()));
    if let Some(since) = since
        && !reported
        && !hid_i2c::sleeping()
        && now - since >= KERNEL_TIMEOUT
        && !matches!(hid_i2c::last_read(), Some(t) if t >= since)
        && hid_i2c::last_read().is_some()
    {
        SHARED.lock(|s| s.hung_reported.set(true));
        warn!("cm0_link: CM0 does not read I2C, it seems hung (power cycle not wired on this board)");
    }

    update_output();
}

fn send(msg: Msg) {
    if TX_QUEUE.try_send(msg).is_err() {
        warn!("cm0_link: tx queue full");
    }
}

/// PC につながっているか：USB に認識されていて、スリープしていない
fn pc_connected() -> bool {
    SHARED.lock(|s| s.usb_configured.get() && !s.usb_suspended.get())
}

fn update_usb(f: impl FnOnce(&Shared)) {
    let before = pc_connected();
    SHARED.lock(f);
    if pc_connected() != before {
        info!("cm0_link: PC {}", if pc_connected() { "connected" } else { "gone" });
        send(Msg::Usb);
        // PC のあるなしで送り先が変わることがある（UI が出ているとき、CM0 が動いていないとき）
        update_output();
    }
}

/// USB の状態の変化（main.rs の UsbStateHandler から）
pub fn set_usb_configured(configured: bool) {
    update_usb(|s| s.usb_configured.set(configured));
}

pub fn set_usb_suspended(suspended: bool) {
    update_usb(|s| s.usb_suspended.set(suspended));
}

/// CM0 にシャットダウンを求める（電源の判定から呼ぶ予定）。I²C の電源キーで頼むので、UI がいなくても、
/// カーネルが動いていれば systemd-logind が `poweroff` する。止まり始めると i2c-hid が `SET_POWER`
/// （SLEEP）を送ってくる（[`hid_i2c::sleeping`]）
#[allow(dead_code)]
pub fn request_shutdown() {
    hid_i2c::press_power_key();
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
        "ALIVE" => ui_seen(),
        "HELLO" => {
            let (configured, suspended) = SHARED.lock(|s| (s.usb_configured.get(), s.usb_suspended.get()));
            info!("cm0_link: HELLO (USB configured {}, suspended {})", configured, suspended);
            reports::release_touch();
            release_touch_keys();
            // UI が出た：PC があればすぐ USB に。返事の OUT で、UI は残るか閉じるかを決める
            ui_seen();
            send(Msg::State);
        }
        "BYE" => {
            // UI が自分で閉じる（Open console）。ALIVE の途切れを待たずに、すぐ I²C へ
            ui_gone("BYE");
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
            write_body(tx, if pc_connected() { "USB,1" } else { "USB,0" }).await;
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
        Msg::User(id, pressed) => {
            let _ = write!(body, "USER,{},{}", id, pressed as u8);
        }
        Msg::Usb => {
            let _ = write!(body, "USB,{}", pc_connected() as u8);
        }
        Msg::Act => {
            SHARED.lock(|s| s.act_pending.set(false));
            let _ = body.push_str("ACT");
        }
    }
    write_body(tx, &body).await;
}

/// 今の送り先（PC：キーボードモード、CM0：サイバーデックモード）。`HELLO` への返事でもあり、
/// UI は `CM0` なら（PC がないので）閉じるか、「PC につないで」を出す
async fn write_msg_out(tx: &mut BufferedUartTx) {
    write_body(tx, if output_to_pc() { "OUT,PC" } else { "OUT,CM0" }).await;
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
    let mut ticker = Ticker::every(SUPERVISE_PERIOD);
    let ticks_per_alive = (ALIVE_PERIOD.as_millis() / SUPERVISE_PERIOD.as_millis()) as u32;
    let mut tick: u32 = 0;
    let mut seq: u8 = 0;
    // 起動したら、まず今の状態（PC のあるなし、CM0 のカーネル）に送り先を合わせる
    update_output();
    loop {
        match select(ticker.next(), TX_QUEUE.receive()).await {
            Either::First(()) => {
                supervise();
                tick += 1;
                if tick >= ticks_per_alive {
                    tick = 0;
                    let mut body: String<16> = String::new();
                    let _ = write!(body, "ALIVE,{}", seq);
                    seq = seq.wrapping_add(1);
                    write_body(&mut tx, &body).await;
                }
            }
            Either::Second(msg) => write_msg(&mut tx, keymap, msg).await,
        }
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
