//! CM0 との UART のやり取り（docs/protocol.md の 1 章）。
//!
//! 1 行 1 メッセージ：`$SK,<コマンド>[,<引数>...]*<CRC-8 の 16 進 2 桁>\n`
//! CM0 の起動中のログ（コンソール）も同じ線に流れてくるので、この形でない行は捨てる。
//!
//! いまできること：
//! - CM0 → RP2040：`HELLO`、`ALIVE`、`KEY`（タッチキー）、`OUT`、`LCD`（記録だけ）、`UF2`
//! - RP2040 → CM0：`ALIVE`、`BLMAX`（いまは 255 固定）、`OUT`、`LAYER`
//! - `PWR` は CC の判定を作るまで送らない。`SHUTDOWN` を出すきっかけもまだない（[`request_shutdown`]）

use core::cell::Cell;
use core::fmt::Write as _;

use embassy_futures::join::join3;
use embassy_futures::select::{Either, select};
use embassy_rp::uart::{BufferedUart, BufferedUartRx, BufferedUartTx};
use embassy_sync::blocking_mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_time::{Duration, Instant, Ticker};
use embedded_io_async::{Read, Write};
use log::{debug, info, warn};
use rmk::event::{EventSubscriber, LayerChangeEvent, SubscribableEvent};
use rmk::heapless::String;

use crate::reports;

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
}

static TX_QUEUE: Channel<CriticalSectionRawMutex, Msg, 8> = Channel::new();

struct Shared {
    out_mode: Cell<OutMode>,
    layer: Cell<u8>,
    /// CM0 から最後に正しい行を受けた時刻。まだなら None
    last_rx: Cell<Option<Instant>>,
}

static SHARED: Mutex<CriticalSectionRawMutex, Shared> = Mutex::new(Shared {
    out_mode: Cell::new(OutMode::Auto),
    layer: Cell::new(0),
    last_rx: Cell::new(None),
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

/// UART を動かし続ける。
pub async fn run(uart: BufferedUart) -> ! {
    let (tx, rx) = uart.split();
    info!("cm0_link: started");
    join3(rx_loop(rx), tx_loop(tx), layer_loop()).await;
    unreachable!()
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
            send(Msg::State);
        }
        "KEY" => {
            let (Some(page), Some(usage)) = (parse_hex(args[0]), parse_hex(args[1])) else {
                warn!("cm0_link: bad KEY {}", body);
                return;
            };
            let down = match args[2] {
                "1" => true,
                "0" => false,
                _ => return,
            };
            reports::touch_key(page as u8, usage, down);
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

async fn write_msg(tx: &mut BufferedUartTx, msg: Msg) {
    let mut body: String<48> = String::new();
    match msg {
        Msg::State => {
            // PWR は CC の判定ができてから送る
            write_body(tx, "BLMAX,255").await;
            write_msg_out(tx).await;
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

async fn tx_loop(mut tx: BufferedUartTx) {
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
            Either::Second(msg) => write_msg(&mut tx, msg).await,
        }
    }
}

/// CM0 から 3 秒届かなければ、押されたままのタッチキーを離す。
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
