//! SKJÁR の RP2040 ファームウェア（試作）。
//!
//! いまの段階で確かめること：RMK のキーボード処理の出力を HID over I²C で
//! Linux（CM0、試作では Pi Zero 2 W）に渡し、`i2c-hid` で普通のキーボードとして使えるか。
//!
//! - キーマトリクスと RMK のキーボード処理（レイヤー、タップ・ホールドなど）
//! - RMK が USB 用に出すレポートを受け取り、タッチキーと合わせて HID over I²C に流す（[`reports`]、[`hid_i2c`]）
//! - CM0 との UART（タッチキー、状態のやり取り。[`cm0_link`]）
//! - USB は、いまは HID にせずログ用のシリアル（CDC-ACM）にする
//!
//! 基板は cargo の feature で選ぶ：`pico`（Raspberry Pi Pico で試す。初期値）か `skjar`。

#![no_std]
#![no_main]

mod cm0_link;
mod hid_i2c;
mod keymap;
mod reports;

use embassy_executor::Spawner;
use embassy_futures::join::join;
use embassy_rp::gpio::{Flex, Input, Level, Output, Pull};
use embassy_rp::i2c_slave::{self, I2cSlave};
use embassy_rp::peripherals::{UART0, USB};
use embassy_rp::uart::{self, BufferedUart};
use embassy_rp::usb::{self, Driver};
use embassy_rp::bind_interrupts;
use log::{info, warn};
use panic_halt as _;
use rmk::channel::USB_REPORT_CHANNEL;
use rmk::config::{BehaviorConfig, PositionalConfig};
use rmk::debounce::default_debouncer::DefaultDebouncer;
use rmk::hid::Report;
use rmk::keyboard::Keyboard;
use rmk::matrix::Matrix;
use rmk::types::connection::UsbState;
use rmk::{KeymapData, initialize_keymap, run_all};
use static_cell::StaticCell;

use crate::keymap::{COL, MATRIX_ROW, NUM_LAYER, ROW};

#[cfg(all(feature = "pico", feature = "skjar"))]
compile_error!("feature の `pico` と `skjar` は、どちらか一方だけを選ぶ");

// キー入力の I²C。SKJÁR は I2C0（GPIO24 / 25）。Pico では GPIO24 / 25 が外に出ていないので
// I2C1（GPIO26 / 27）を使う。
#[cfg(feature = "skjar")]
type KbI2c = embassy_rp::peripherals::I2C0;
#[cfg(not(feature = "skjar"))]
type KbI2c = embassy_rp::peripherals::I2C1;

#[cfg(feature = "skjar")]
bind_interrupts!(struct Irqs {
    USBCTRL_IRQ => usb::InterruptHandler<USB>;
    I2C0_IRQ => embassy_rp::i2c::InterruptHandler<KbI2c>;
    UART0_IRQ => uart::BufferedInterruptHandler<UART0>;
});
#[cfg(not(feature = "skjar"))]
bind_interrupts!(struct Irqs {
    USBCTRL_IRQ => usb::InterruptHandler<USB>;
    I2C1_IRQ => embassy_rp::i2c::InterruptHandler<KbI2c>;
    UART0_IRQ => uart::BufferedInterruptHandler<UART0>;
});

#[embassy_executor::task]
async fn logger_task(driver: Driver<'static, USB>) {
    embassy_usb_logger::run!(1024, log::LevelFilter::Debug, driver);
}

#[embassy_executor::task]
async fn hid_i2c_task(i2c: I2cSlave<'static, KbI2c>) {
    hid_i2c::run_target(i2c).await
}

/// RMK が出すレポートを、タッチキーと合わせて出力先に送る。
#[embassy_executor::task]
async fn report_bridge_task() {
    loop {
        match USB_REPORT_CHANNEL.receive().await {
            Report::KeyboardReport(r) => reports::rmk_keyboard(r.modifier, r.keycodes),
            Report::MediaKeyboardReport(r) => reports::rmk_consumer(r.usage_id),
            Report::MouseReport(r) => reports::rmk_mouse(r.buttons, r.x, r.y, r.wheel, r.pan),
            _ => warn!("bridge: report type not supported over I2C yet"),
        }
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_rp::init(Default::default());

    // ログ（USB の仮想シリアル）
    spawner.spawn(logger_task(Driver::new(p.USB, Irqs)).unwrap());

    // KB_INT（GPIO13）：Low で CM0 に知らせる。オープンドレインを真似る（hid_i2c 参照）。
    hid_i2c::init_int_pin(Flex::new(p.PIN_13));

    // キー入力の I²C。内部プルアップは使わない（プルアップは CM0 の 3.3V 側にある）。
    let mut i2c_config = i2c_slave::Config::default();
    i2c_config.addr = hid_i2c::I2C_ADDR;
    i2c_config.general_call = false;
    i2c_config.sda_pullup = false;
    i2c_config.scl_pullup = false;
    #[cfg(feature = "skjar")]
    let i2c = I2cSlave::new(p.I2C0, p.PIN_25, p.PIN_24, Irqs, i2c_config);
    #[cfg(not(feature = "skjar"))]
    let i2c = I2cSlave::new(p.I2C1, p.PIN_27, p.PIN_26, Irqs, i2c_config);
    spawner.spawn(hid_i2c_task(i2c).unwrap());

    // CM0 との UART（UART0：GPIO16 TX → CM0 の GPIO15、GPIO17 RX ← CM0 の GPIO14）。
    // TX は CM0 の 3.3V で動くバッファを通るので、CM0 が OFF でも電流は流れ込まない。
    // RX には内部のプルを付けない（embassy-rp の初期化はプルを有効にしない）。
    static UART_TX_BUF: StaticCell<[u8; 256]> = StaticCell::new();
    static UART_RX_BUF: StaticCell<[u8; 256]> = StaticCell::new();
    let mut uart_config = uart::Config::default();
    uart_config.baudrate = 115_200;
    let uart = BufferedUart::new(
        p.UART0,
        p.PIN_16,
        p.PIN_17,
        Irqs,
        UART_TX_BUF.init([0; 256]),
        UART_RX_BUF.init([0; 256]),
        uart_config,
    );
    // CM0 とのやり取りはキーマップを読むので、キーマップを作ったあと、キーボードと並べて動かす（下）

    // RMK は USB がつながったときだけ USB 用のチャンネルにレポートを出す。
    // いまは USB の HID を使わないので、つながったことにしてチャンネルを横取りする。
    rmk::state::set_usb_state(UsbState::Configured);
    spawner.spawn(report_bridge_task().unwrap());

    // キーマトリクス（7 行 × 6 列、ダイオードは列 → 行）。キーマップは、これにタッチキーの 4 行を足した 11 行（keymap.rs）。
    // RMK の COL2ROW は、列を High に駆動して行を読む（行はプルダウン）。
    let rows = [
        Input::new(p.PIN_10, Pull::Down), // ROW0
        Input::new(p.PIN_6, Pull::Down),  // ROW1
        Input::new(p.PIN_11, Pull::Down), // ROW2
        Input::new(p.PIN_7, Pull::Down),  // ROW3
        Input::new(p.PIN_12, Pull::Down), // ROW4
        Input::new(p.PIN_8, Pull::Down),  // ROW5
        Input::new(p.PIN_9, Pull::Down),  // ROW6
    ];
    let cols = [
        Output::new(p.PIN_4, Level::Low), // COL0
        Output::new(p.PIN_5, Level::Low), // COL1
        Output::new(p.PIN_3, Level::Low), // COL2
        Output::new(p.PIN_2, Level::Low), // COL3
        Output::new(p.PIN_1, Level::Low), // COL4
        Output::new(p.PIN_0, Level::Low), // COL5
    ];
    let mut matrix = Matrix::<_, _, _, MATRIX_ROW, COL, true>::new(rows, cols, DefaultDebouncer::new());

    static KEYMAP_DATA: StaticCell<KeymapData<ROW, COL, NUM_LAYER, 0>> = StaticCell::new();
    static BEHAVIOR: StaticCell<BehaviorConfig> = StaticCell::new();
    static POSITIONAL: StaticCell<PositionalConfig<ROW, COL>> = StaticCell::new();
    let keymap_data = KEYMAP_DATA.init(KeymapData::new(keymap::get_default_keymap()));
    let behavior = BEHAVIOR.init(BehaviorConfig::default());
    let positional = POSITIONAL.init(PositionalConfig::default());
    let keymap = initialize_keymap(keymap_data, behavior, positional).await;
    let mut keyboard = Keyboard::new(&keymap);

    info!("SKJÁR RP2040 started");
    join(run_all!(matrix, keyboard), cm0_link::run(uart, &keymap)).await;
}
