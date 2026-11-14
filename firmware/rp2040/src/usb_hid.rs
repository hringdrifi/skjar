//! PC への USB（Port A）：キーボード、マウス・メディアキー、ログ用の仮想シリアル。
//!
//! RMK の `UsbTransport` は使わない。あれは RMK のレポートのチャンネルを自分で読んで PC に送るが、
//! SKJÁR はそのチャンネルを横取りして、タッチキーと合わせ、モードで PC と CM0 に送り分ける
//! （[`crate::reports`]）。そこで USB の口はここで組み、送るものは [`push`] で受け取る。
//! HID のディスクリプタは RMK のもの（`KeyboardReport`、`CompositeReport`）をそのまま使うので、
//! PC からは RMK のキーボードと同じに見える。
//!
//! - インターフェース 0：キーボード（ブート対応。修飾キー、予約、キー 6 個の 8 バイト。PC から LED の状態を受ける）
//! - インターフェース 1：マウス（レポート ID 2）、Consumer（3）、System Control（4）
//! - インターフェース 2・3：ログ用の仮想シリアル（CDC-ACM）

use embassy_futures::join::join4;
use embassy_rp::peripherals::USB;
use embassy_rp::usb::Driver;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_time::{Duration, with_timeout};
use embassy_usb::class::cdc_acm::{CdcAcmClass, State as CdcAcmState};
use embassy_usb::class::hid::{
    Config as HidConfig, HidBootProtocol, HidReaderWriter, HidSubclass, HidWriter, ReportId, RequestHandler,
    State as HidState,
};
use embassy_usb::control::OutResponse;
use log::{info, warn};
use rmk::hid::{CompositeReport, KeyboardReport};
use static_cell::StaticCell;
use usbd_hid::descriptor::SerializedDescriptor;

use crate::cm0_link;

/// PC に送るレポート
#[derive(Clone, Copy)]
pub enum UsbReport {
    Keyboard { modifier: u8, keys: [u8; 6] },
    Consumer(u16),
    Mouse { buttons: u8, x: i8, y: i8, wheel: i8, pan: i8 },
}

static OUT: Channel<CriticalSectionRawMutex, UsbReport, 16> = Channel::new();

/// PC が読みに来ない（つながっていない、止まっている）ときに、送るのを待ちすぎない
const WRITE_TIMEOUT: Duration = Duration::from_millis(100);

/// PC に送る（待たない）。いっぱいなら捨てる
pub fn push(report: UsbReport) {
    if OUT.try_send(report).is_err() {
        warn!("usb_hid: queue full, dropping a report");
    }
}

/// CompositeReport のレポート ID（RMK の `CompositeReportType` と同じ）
const ID_MOUSE: u8 = 0x02;
const ID_MEDIA: u8 = 0x03;

pub async fn run(driver: Driver<'static, USB>) {
    static CONFIG_DESC: StaticCell<[u8; 256]> = StaticCell::new();
    static BOS_DESC: StaticCell<[u8; 64]> = StaticCell::new();
    static MSOS_DESC: StaticCell<[u8; 64]> = StaticCell::new();
    static CONTROL_BUF: StaticCell<[u8; 128]> = StaticCell::new();
    static HANDLER: StaticCell<UsbStateHandler> = StaticCell::new();
    static KEYBOARD_STATE: StaticCell<HidState> = StaticCell::new();
    static OTHER_STATE: StaticCell<HidState> = StaticCell::new();
    static LED_HANDLER: StaticCell<LedHandler> = StaticCell::new();
    static LED_CONTROL_HANDLER: StaticCell<LedHandler> = StaticCell::new();
    static CDC_STATE: StaticCell<CdcAcmState> = StaticCell::new();

    // pid.codes の試験用の番号（HID over I²C の 1209:0001 と同じ）。製品にするときは取り直す
    let mut config = embassy_usb::Config::new(0x1209, 0x0001);
    config.manufacturer = Some("SKJAR");
    config.product = Some("SKJAR");
    config.max_power = 100;
    config.max_packet_size_0 = 64;
    // HID と CDC を 1 つの機器にまとめる（Windows で必要）
    config.device_class = 0xEF;
    config.device_sub_class = 0x02;
    config.device_protocol = 0x01;
    config.composite_with_iads = true;

    let mut builder = embassy_usb::Builder::new(
        driver,
        config,
        CONFIG_DESC.init([0; 256]),
        BOS_DESC.init([0; 64]),
        MSOS_DESC.init([0; 64]),
        CONTROL_BUF.init([0; 128]),
    );
    builder.handler(HANDLER.init(UsbStateHandler));

    // キーボード。Linux の usbhid がブート対応のキーボードとして扱うよう、Boot / Keyboard にする
    let keyboard = HidReaderWriter::<_, 1, 8>::new(
        &mut builder,
        KEYBOARD_STATE.init(HidState::new()),
        HidConfig {
            report_descriptor: KeyboardReport::desc(),
            // LED の状態は、割り込みの OUT（下の reader）でも、制御の転送（SET_REPORT）でも来る
            request_handler: Some(LED_CONTROL_HANDLER.init(LedHandler)),
            poll_ms: 1,
            max_packet_size: 8,
            hid_subclass: HidSubclass::Boot,
            hid_boot_protocol: HidBootProtocol::Keyboard,
        },
    );
    let mut other = HidWriter::<_, 9>::new(
        &mut builder,
        OTHER_STATE.init(HidState::new()),
        HidConfig {
            report_descriptor: CompositeReport::desc(),
            request_handler: None,
            poll_ms: 1,
            max_packet_size: 16,
            hid_subclass: HidSubclass::No,
            hid_boot_protocol: HidBootProtocol::None,
        },
    );
    let log_class = CdcAcmClass::new(&mut builder, CDC_STATE.init(CdcAcmState::new()), 64);
    let mut usb = builder.build();

    let log = embassy_usb_logger::with_class!(1024, log::LevelFilter::Debug, log_class);
    let (reader, mut keyboard_writer) = keyboard.split();
    // PC からの LED の状態（Caps Lock など）。いまはログに出すだけ
    let leds = reader.run(false, LED_HANDLER.init(LedHandler));

    let writer = async {
        loop {
            let report = OUT.receive().await;
            let result = match report {
                UsbReport::Keyboard { modifier, keys } => {
                    let mut buf = [0u8; 8];
                    buf[0] = modifier;
                    buf[2..8].copy_from_slice(&keys);
                    with_timeout(WRITE_TIMEOUT, keyboard_writer.write(&buf)).await
                }
                UsbReport::Consumer(usage) => {
                    let u = usage.to_le_bytes();
                    with_timeout(WRITE_TIMEOUT, other.write(&[ID_MEDIA, u[0], u[1]])).await
                }
                UsbReport::Mouse { buttons, x, y, wheel, pan } => {
                    let buf = [ID_MOUSE, buttons, x as u8, y as u8, wheel as u8, pan as u8];
                    with_timeout(WRITE_TIMEOUT, other.write(&buf)).await
                }
            };
            match result {
                Ok(Ok(())) => {}
                Ok(Err(e)) => warn!("usb_hid: write failed: {:?}", e),
                Err(_) => warn!("usb_hid: the PC is not reading, dropped a report"),
            }
        }
    };

    join4(usb.run(), log, leds, writer).await;
}

/// PC が送ってくる LED の状態（出力レポート）
struct LedHandler;

impl RequestHandler for LedHandler {
    fn set_report(&mut self, _id: ReportId, data: &[u8]) -> OutResponse {
        if let Some(leds) = data.first() {
            info!("usb_hid: LEDs = 0b{:05b}", leds);
        }
        OutResponse::Accepted
    }
}

/// USB の状態の変化を cm0_link に伝える（PC に認識されたか、スリープしたか、抜かれたか）
struct UsbStateHandler;

impl embassy_usb::Handler for UsbStateHandler {
    fn enabled(&mut self, enabled: bool) {
        if !enabled {
            cm0_link::set_usb_configured(false);
        }
    }
    // リセットや認識のときは、スリープからも覚めている。embassy-usb はこのとき suspended(false) を
    // 呼ばないので、つなぐ前（USB の信号がない間）に来た suspended(true) が残り、PC につながって
    // いても「スリープ中」のままになっていた（XIAO RP2040 で、2026-11-12）
    fn reset(&mut self) {
        cm0_link::set_usb_suspended(false);
        cm0_link::set_usb_configured(false);
    }
    fn configured(&mut self, configured: bool) {
        cm0_link::set_usb_suspended(false);
        cm0_link::set_usb_configured(configured);
    }
    fn suspended(&mut self, suspended: bool) {
        cm0_link::set_usb_suspended(suspended);
    }
}
