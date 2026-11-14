//! HID over I²C のターゲット（docs/protocol.md の 2 章）。
//!
//! RP2040 が I²C の HID デバイスになり、Linux の `i2c-hid` ドライバから
//! 普通のキーボードに見えるようにする。プロトコルは Microsoft の
//! 「HID over I2C Protocol Specification」v1.0 に従う。
//!
//! - [`run_target`]：I²C のターゲット。CM0 からの読み書きに答える。
//! - [`push_input`]：入力レポートを積み、`KB_INT` を Low にして CM0 に知らせる。
//!
//! `KB_INT` は出力値を 0 に固定し、出力の有効・無効だけを切り替えて
//! オープンドレインを真似る（High を出すと、OFF の CM0 に電流を流し込むため）。

use core::cell::RefCell;

use embassy_rp::gpio::{Flex, Pull};
use embassy_rp::i2c::Instance;
use embassy_rp::i2c_slave::{Command, I2cSlave, ReadStatus};
use embassy_sync::blocking_mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_time::Instant;
use log::{debug, info, warn};
use rmk::heapless::Deque;

/// I²C のアドレス（7 ビット）。このバスには RP2040 しかいないので自由に決めてよい。
pub const I2C_ADDR: u16 = 0x2C;

// レジスタの番地。CM0 に伝えるのは HID ディスクリプタの番地だけ（デバイスツリーの
// `hid-descr-addr`）。ほかは HID ディスクリプタの中で知らせる。
const REG_HID_DESC: u16 = 0x0001;
const REG_REPORT_DESC: u16 = 0x0002;
const REG_INPUT: u16 = 0x0003;
const REG_OUTPUT: u16 = 0x0004;
const REG_COMMAND: u16 = 0x0005;
const REG_DATA: u16 = 0x0006;

/// pid.codes のテスト用の VID / PID。製品の値は未定。
const VID: u16 = 0x1209;
const PID: u16 = 0x0001;
const VERSION: u16 = 0x0001;

const REPORT_ID_KEYBOARD: u8 = 1;
const REPORT_ID_CONSUMER: u8 = 2;
const REPORT_ID_MOUSE: u8 = 3;

/// 入力レポートの最大の長さ（長さ 2 ＋ レポート ID 1 ＋ キーボード 8）。
const MAX_INPUT_LEN: usize = 11;
/// 出力レポートの最大の長さ（長さ 2 ＋ レポート ID 1 ＋ LED 1）。
const MAX_OUTPUT_LEN: usize = 4;

#[rustfmt::skip]
const REPORT_DESCRIPTOR: &[u8] = &[
    // キーボード（レポート ID 1）
    0x05, 0x01,             // Usage Page (Generic Desktop)
    0x09, 0x06,             // Usage (Keyboard)
    0xA1, 0x01,             // Collection (Application)
    0x85, REPORT_ID_KEYBOARD, //   Report ID (1)
    0x05, 0x07,             //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,             //   Usage Minimum (Left Control)
    0x29, 0xE7,             //   Usage Maximum (Right GUI)
    0x15, 0x00,             //   Logical Minimum (0)
    0x25, 0x01,             //   Logical Maximum (1)
    0x75, 0x01,             //   Report Size (1)
    0x95, 0x08,             //   Report Count (8)
    0x81, 0x02,             //   Input (Data, Var, Abs)：修飾キー
    0x75, 0x08,             //   Report Size (8)
    0x95, 0x01,             //   Report Count (1)
    0x81, 0x01,             //   Input (Const)：予約
    0x05, 0x08,             //   Usage Page (LEDs)
    0x19, 0x01,             //   Usage Minimum (Num Lock)
    0x29, 0x05,             //   Usage Maximum (Kana)
    0x75, 0x01,             //   Report Size (1)
    0x95, 0x05,             //   Report Count (5)
    0x91, 0x02,             //   Output (Data, Var, Abs)：LED
    0x75, 0x03,             //   Report Size (3)
    0x95, 0x01,             //   Report Count (1)
    0x91, 0x01,             //   Output (Const)：詰め物
    0x05, 0x07,             //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,             //   Usage Minimum (0)
    0x2A, 0xFF, 0x00,       //   Usage Maximum (255)
    0x15, 0x00,             //   Logical Minimum (0)
    0x26, 0xFF, 0x00,       //   Logical Maximum (255)
    0x75, 0x08,             //   Report Size (8)
    0x95, 0x06,             //   Report Count (6)
    0x81, 0x00,             //   Input (Data, Array)：キー 6 個
    0xC0,                   // End Collection

    // Consumer Control（レポート ID 2）
    0x05, 0x0C,             // Usage Page (Consumer)
    0x09, 0x01,             // Usage (Consumer Control)
    0xA1, 0x01,             // Collection (Application)
    0x85, REPORT_ID_CONSUMER, //   Report ID (2)
    0x19, 0x00,             //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,       //   Usage Maximum (0x3FF)
    0x15, 0x00,             //   Logical Minimum (0)
    0x26, 0xFF, 0x03,       //   Logical Maximum (0x3FF)
    0x75, 0x10,             //   Report Size (16)
    0x95, 0x01,             //   Report Count (1)
    0x81, 0x00,             //   Input (Data, Array)
    0xC0,                   // End Collection

    // マウス（レポート ID 3。トラックパッドと RMK のマウスキー）
    0x05, 0x01,             // Usage Page (Generic Desktop)
    0x09, 0x02,             // Usage (Mouse)
    0xA1, 0x01,             // Collection (Application)
    0x85, REPORT_ID_MOUSE,  //   Report ID (3)
    0x09, 0x01,             //   Usage (Pointer)
    0xA1, 0x00,             //   Collection (Physical)
    0x05, 0x09,             //     Usage Page (Button)
    0x19, 0x01,             //     Usage Minimum (1)
    0x29, 0x05,             //     Usage Maximum (5)
    0x15, 0x00,             //     Logical Minimum (0)
    0x25, 0x01,             //     Logical Maximum (1)
    0x75, 0x01,             //     Report Size (1)
    0x95, 0x05,             //     Report Count (5)
    0x81, 0x02,             //     Input (Data, Var, Abs)：ボタン 5 個
    0x75, 0x03,             //     Report Size (3)
    0x95, 0x01,             //     Report Count (1)
    0x81, 0x01,             //     Input (Const)：詰め物
    0x05, 0x01,             //     Usage Page (Generic Desktop)
    0x09, 0x30,             //     Usage (X)
    0x09, 0x31,             //     Usage (Y)
    0x09, 0x38,             //     Usage (Wheel)
    0x15, 0x81,             //     Logical Minimum (-127)
    0x25, 0x7F,             //     Logical Maximum (127)
    0x75, 0x08,             //     Report Size (8)
    0x95, 0x03,             //     Report Count (3)
    0x81, 0x06,             //     Input (Data, Var, Rel)
    0x05, 0x0C,             //     Usage Page (Consumer)
    0x0A, 0x38, 0x02,       //     Usage (AC Pan)
    0x95, 0x01,             //     Report Count (1)
    0x81, 0x06,             //     Input (Data, Var, Rel)：横のスクロール
    0xC0,                   //   End Collection
    0xC0,                   // End Collection
];

const fn le(v: u16) -> [u8; 2] {
    v.to_le_bytes()
}

/// HID ディスクリプタ（30 バイト）。
const HID_DESCRIPTOR: [u8; 30] = {
    let f: [[u8; 2]; 13] = [
        le(30),                             // wHIDDescLength
        le(0x0100),                         // bcdVersion
        le(REPORT_DESCRIPTOR.len() as u16), // wReportDescLength
        le(REG_REPORT_DESC),                // wReportDescRegister
        le(REG_INPUT),                      // wInputRegister
        le(MAX_INPUT_LEN as u16),           // wMaxInputLength
        le(REG_OUTPUT),                     // wOutputRegister
        le(MAX_OUTPUT_LEN as u16),          // wMaxOutputLength
        le(REG_COMMAND),                    // wCommandRegister
        le(REG_DATA),                       // wDataRegister
        le(VID),                            // wVendorID
        le(PID),                            // wProductID
        le(VERSION),                        // wVersionID
    ];
    let mut out = [0u8; 30]; // 最後の 4 バイトは予約（0）
    let mut i = 0;
    while i < 13 {
        out[2 * i] = f[i][0];
        out[2 * i + 1] = f[i][1];
        i += 1;
    }
    out
};

// コマンドのオペコード（コマンドレジスタに書かれる 2 バイト目の下位 4 ビット）
const OP_RESET: u8 = 0x01;
const OP_GET_REPORT: u8 = 0x02;
const OP_SET_REPORT: u8 = 0x03;
const OP_SET_POWER: u8 = 0x08;

const REPORT_TYPE_INPUT: u8 = 1;
const REPORT_TYPE_OUTPUT: u8 = 2;

/// 長さの 2 バイトを先頭に付けた、そのまま送れる入力レポート。
#[derive(Clone, Copy)]
pub struct InputReport {
    buf: [u8; MAX_INPUT_LEN],
}

impl InputReport {
    /// レポート ID と中身から作る。
    fn new(id: u8, payload: &[u8]) -> Self {
        let len = 3 + payload.len();
        let mut buf = [0u8; MAX_INPUT_LEN];
        buf[..2].copy_from_slice(&(len as u16).to_le_bytes());
        buf[2] = id;
        buf[3..len].copy_from_slice(payload);
        Self { buf }
    }

    pub fn keyboard(modifier: u8, keycodes: [u8; 6]) -> Self {
        let mut p = [0u8; 8];
        p[0] = modifier;
        p[2..].copy_from_slice(&keycodes);
        Self::new(REPORT_ID_KEYBOARD, &p)
    }

    pub fn consumer(usage: u16) -> Self {
        Self::new(REPORT_ID_CONSUMER, &usage.to_le_bytes())
    }

    /// x / y / wheel / pan は相対値。wheel は正で上、pan は正で右。
    pub fn mouse(buttons: u8, x: i8, y: i8, wheel: i8, pan: i8) -> Self {
        Self::new(REPORT_ID_MOUSE, &[buttons, x as u8, y as u8, wheel as u8, pan as u8])
    }

    fn len(&self) -> usize {
        u16::from_le_bytes([self.buf[0], self.buf[1]]) as usize
    }

    fn bytes(&self) -> &[u8] {
        &self.buf[..self.len()]
    }
}

/// 長さ 0 の入力（`RESET` への返事と、送るものがないとき）。
const EMPTY_INPUT: [u8; 2] = [0, 0];

const QUEUE_LEN: usize = 32;

struct State {
    /// 入力を積むか。初めから true（`RESET` を待たない）：RP2040 だけが再起動したとき、i2c-hid は
    /// `RESET` を送り直さないので、待つとキーが効かなくなる（2026-11-12）。Linux の起動時は、
    /// i2c-hid が `RESET` で積んだものを捨てさせるので困らない。`SET_POWER`（SLEEP）のあとは
    /// false（CM0 は止まる途中）で、`RESET` か `SET_POWER`（ON）で true に戻る
    ready: bool,
    /// CM0 のカーネル（i2c-hid）が入力を最後に読み切った時刻。生きているかの目安（cm0_link.rs）
    last_read: Option<Instant>,
    /// `RESET` を受けて、長さ 0 の入力をまだ読まれていない。
    reset_pending: bool,
    /// いま CM0 に読ませようとしているレポート。読み切られるまで残す。
    current: Option<InputReport>,
    queue: Deque<InputReport, QUEUE_LEN>,
    int_pin: Option<Flex<'static>>,
    leds: u8,
}

impl State {
    const fn new() -> Self {
        Self {
            ready: true,
            last_read: None,
            reset_pending: false,
            current: None,
            queue: Deque::new(),
            int_pin: None,
            leds: 0,
        }
    }

    /// 読ませるものがあるときだけ `KB_INT` を Low に引く。
    fn update_int(&mut self) {
        let assert = self.reset_pending || self.current.is_some() || !self.queue.is_empty();
        if let Some(pin) = self.int_pin.as_mut() {
            // 出力値は常に 0。出力を有効にすると Low、無効にすると離す（プルアップは CM0 側）。
            if assert {
                pin.set_as_output();
            } else {
                pin.set_as_input();
            }
        }
    }
}

static STATE: Mutex<CriticalSectionRawMutex, RefCell<State>> = Mutex::new(RefCell::new(State::new()));

fn with_state<R>(f: impl FnOnce(&mut State) -> R) -> R {
    STATE.lock(|s| f(&mut s.borrow_mut()))
}

/// `KB_INT` のピンを預ける。最初に一度だけ呼ぶ。
pub fn init_int_pin(mut pin: Flex<'static>) {
    pin.set_pull(Pull::None); // 内部のプル（初期設定のプルダウンも）は使わない
    pin.set_low(); // 出力値は 0 に固定
    pin.set_as_input(); // 離した状態から始める
    with_state(|s| {
        s.int_pin = Some(pin);
        s.update_int();
    });
}

/// 入力レポートを積む。CM0 が止まる途中（`SET_POWER` SLEEP のあと）は捨てる。
pub fn push_input(report: InputReport) {
    with_state(|s| {
        if !s.ready {
            return;
        }
        if s.queue.is_full() {
            // キーボードと Consumer のレポートはその時点の全部の状態を持つので、古いものから捨てる。
            // マウスの動きは相対値なので、捨てた分だけカーソルが進まない（CM0 が読みに来ない間だけ起きる）。
            warn!("hid_i2c: queue full, dropping the oldest report");
            s.queue.pop_front();
        }
        let _ = s.queue.push_back(report);
        s.update_int();
    });
}

/// CM0 のカーネルが入力を最後に読み切った時刻（まだなら None）
pub fn last_read() -> Option<Instant> {
    with_state(|s| s.last_read)
}

/// まだ読まれていない入力があるか
pub fn pending() -> bool {
    with_state(|s| s.current.is_some() || !s.queue.is_empty())
}

/// CM0 が止まる途中か（i2c-hid が `SET_POWER`（SLEEP）を送ってきて、まだ `RESET` などが来ていない）。
/// シャットダウンや再起動のとき、カーネルが送ってくる
pub fn sleeping() -> bool {
    with_state(|s| !s.ready)
}

/// Consumer の Power（0x30）。Linux では `KEY_POWER` になり、systemd-logind が `poweroff` する
const CONSUMER_POWER: u16 = 0x30;

/// 電源キーを押して離したことにする（CM0 にシャットダウンを頼む）。UI が動いていなくても、
/// カーネルが動いていれば効く
pub fn press_power_key() {
    info!("hid_i2c: power key");
    push_input(InputReport::consumer(CONSUMER_POWER));
    push_input(InputReport::consumer(0));
}

/// CM0 から最後に受け取ったキーボードの LED（bit0 Num、bit1 Caps、bit2 Scroll …）。
#[allow(dead_code)]
pub fn leds() -> u8 {
    with_state(|s| s.leds)
}

/// I²C のターゲットを動かし続ける。
pub async fn run_target<T: Instance>(mut i2c: I2cSlave<'static, T>) -> ! {
    info!("hid_i2c: listening at 0x{:02X}", I2C_ADDR);
    let mut buf = [0u8; 64];
    loop {
        match i2c.listen(&mut buf).await {
            Ok(Command::Read) => respond_input(&mut i2c).await,
            Ok(Command::WriteRead(n)) => handle_write_read(&mut i2c, &buf[..n]).await,
            Ok(Command::Write(n)) => handle_write(&buf[..n]),
            Ok(Command::GeneralCall(_)) => {}
            Err(e) => warn!("hid_i2c: listen error {:?}", e),
        }
    }
}

/// 番地なしの読み出し（＝入力レジスタ）。読み切られたら、そのレポートを片付ける。
async fn respond_input<T: Instance>(i2c: &mut I2cSlave<'static, T>) {
    let (resp, was_reset) = with_state(|s| {
        if s.reset_pending {
            return (None, true);
        }
        if s.current.is_none() {
            s.current = s.queue.pop_front();
        }
        (s.current, false)
    });
    let bytes: &[u8] = match &resp {
        Some(r) => r.bytes(),
        None => &EMPTY_INPUT,
    };
    // Linux は毎回 wMaxInputLength だけ読むので、足りない分は 0 で埋める。
    match i2c.respond_and_fill(bytes, 0).await {
        Ok(status) => {
            let sent = match status {
                ReadStatus::LeftoverBytes(left) => bytes.len().saturating_sub(left as usize),
                _ => bytes.len(),
            };
            // 1 バイトだけの読み出し（Linux が起動時にデバイスの有無を見るもの）などでは片付けない。
            if sent >= bytes.len() {
                with_state(|s| {
                    s.last_read = Some(Instant::now());
                    if was_reset {
                        s.reset_pending = false;
                        debug!("hid_i2c: reset acknowledged");
                    } else if resp.is_some() {
                        s.current = None;
                    }
                    s.update_int();
                });
            }
        }
        Err(e) => warn!("hid_i2c: respond error {:?}", e),
    }
}

async fn handle_write_read<T: Instance>(i2c: &mut I2cSlave<'static, T>, w: &[u8]) {
    if w.len() < 2 {
        let _ = i2c.respond_till_stop(0).await;
        return;
    }
    let reg = u16::from_le_bytes([w[0], w[1]]);
    let result = match reg {
        REG_HID_DESC => {
            debug!("hid_i2c: HID descriptor read");
            i2c.respond_and_fill(&HID_DESCRIPTOR, 0).await
        }
        REG_REPORT_DESC => {
            debug!("hid_i2c: report descriptor read");
            i2c.respond_and_fill(REPORT_DESCRIPTOR, 0).await
        }
        REG_INPUT => {
            respond_input(i2c).await;
            return;
        }
        REG_COMMAND => {
            let cmd = parse_command(w);
            if cmd.opcode == OP_GET_REPORT {
                let resp = get_report(cmd.report_type, cmd.report_id);
                debug!("hid_i2c: GET_REPORT type={} id={}", cmd.report_type, cmd.report_id);
                i2c.respond_and_fill(resp.bytes(), 0).await
            } else {
                warn!("hid_i2c: unexpected read after opcode {}", cmd.opcode);
                i2c.respond_and_fill(&EMPTY_INPUT, 0).await
            }
        }
        _ => {
            warn!("hid_i2c: read from unknown register 0x{:04X}", reg);
            i2c.respond_and_fill(&EMPTY_INPUT, 0).await
        }
    };
    if let Err(e) = result {
        warn!("hid_i2c: respond error {:?}", e);
    }
}

fn handle_write(w: &[u8]) {
    if w.len() < 2 {
        return;
    }
    let reg = u16::from_le_bytes([w[0], w[1]]);
    match reg {
        REG_COMMAND => {
            let cmd = parse_command(w);
            match cmd.opcode {
                OP_RESET => {
                    info!("hid_i2c: RESET");
                    with_state(|s| {
                        s.queue.clear();
                        s.current = None;
                        s.reset_pending = true;
                        s.ready = true;
                        s.update_int();
                    });
                }
                OP_SET_POWER => {
                    // SET_POWER では、レポート ID の欄に電源の状態が入る（0：ON、1：SLEEP）。
                    let on = cmd.report_id == 0;
                    info!("hid_i2c: SET_POWER {}", if on { "ON" } else { "SLEEP" });
                    with_state(|s| {
                        s.ready = on;
                        if !on {
                            s.queue.clear();
                            s.current = None;
                        }
                        s.update_int();
                    });
                }
                OP_SET_REPORT => {
                    // 続きは データレジスタ（2）、長さ（2）、レポート ID、中身
                    let rest = &w[cmd.rest..];
                    if rest.len() >= 5 {
                        set_output_report(cmd.report_type, rest[4], &rest[5..]);
                    }
                }
                op => debug!("hid_i2c: ignoring opcode {}", op),
            }
        }
        REG_OUTPUT => {
            // 長さ（2）、レポート ID、中身
            let rest = &w[2..];
            if rest.len() >= 3 {
                set_output_report(REPORT_TYPE_OUTPUT, rest[2], &rest[3..]);
            }
        }
        _ => warn!("hid_i2c: write to unknown register 0x{:04X}", reg),
    }
}

struct ParsedCommand {
    opcode: u8,
    report_type: u8,
    report_id: u8,
    /// コマンドの後に続くバイトの位置
    rest: usize,
}

fn parse_command(w: &[u8]) -> ParsedCommand {
    let lo = w.get(2).copied().unwrap_or(0);
    let hi = w.get(3).copied().unwrap_or(0);
    let mut id = lo & 0x0F;
    let mut rest = 4;
    // レポート ID が 15 以上のときは 0xF を入れ、次のバイトに本当の ID を書く。
    if id == 0x0F {
        id = w.get(4).copied().unwrap_or(0);
        rest = 5;
    }
    ParsedCommand {
        opcode: hi & 0x0F,
        report_type: (lo >> 4) & 0x03,
        report_id: id,
        rest: rest.min(w.len()),
    }
}

fn set_output_report(report_type: u8, id: u8, data: &[u8]) {
    if report_type == REPORT_TYPE_OUTPUT && id == REPORT_ID_KEYBOARD {
        if let Some(&leds) = data.first() {
            info!("hid_i2c: LEDs = 0b{:05b}", leds);
            with_state(|s| s.leds = leds);
        }
    } else {
        debug!("hid_i2c: ignoring SET_REPORT type={} id={}", report_type, id);
    }
}

fn get_report(report_type: u8, id: u8) -> InputReport {
    match (report_type, id) {
        (REPORT_TYPE_INPUT, REPORT_ID_CONSUMER) => InputReport::consumer(0),
        (REPORT_TYPE_INPUT, REPORT_ID_MOUSE) => InputReport::mouse(0, 0, 0, 0, 0),
        (REPORT_TYPE_OUTPUT, REPORT_ID_KEYBOARD) => InputReport::new(REPORT_ID_KEYBOARD, &[with_state(|s| s.leds)]),
        _ => InputReport::keyboard(0, [0; 6]),
    }
}
