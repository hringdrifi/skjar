//! キーの RGB（SK6812MINI-E × 42。docs/design-decisions.md の 6.1）。
//!
//! いまは隠し機能の動画（CM0 の UI が時計を 5 回叩くと流す）に合わせて光らせるだけ。CM0 から UART の
//! `LEDF` で 1 コマ分の明るさ（42 キー × 0〜15）が届くと、白で光らせる。届かなくなって
//! [`IDLE_OFF`] たったら消して、LED の電源（`RGB_EN`）を切る。
//!
//! - データ線：GPIO14（`RGB_DATA`。PIO0 の SM0 と DMA_CH0 で出す）。基板では SN74AHCT1G125 で 5 V にする
//! - 電源：GPIO20（`RGB_EN`。High で ON）。電源を切っている間、データ線は Low のまま（PIO は送り終わると Low）
//! - 明るさの上限：全点灯で約 1.5 A になるので、ここでは 1 色あたり [`MAX_LEVEL`] に抑える。
//!   電流宣言ごとの上限（docs/design-decisions.md の 2.4）は、普段の光らせ方を作るときに決める

use embassy_rp::gpio::Output;
use embassy_rp::peripherals::PIO0;
use embassy_rp::pio_programs::ws2812::{Grb, PioWs2812};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;
use embassy_time::{Duration, Timer, with_timeout};
use log::info;
use smart_leds::RGB8;

pub const LEDS: usize = 42;

/// 1 色あたりの最大（0〜255）。42 個が白で最大のとき約 0.5 A
const MAX_LEVEL: u32 = 48;
/// 電源を入れてから光らせるまで待つ時間（ゲートのコンデンサでゆっくり ON にしているため）
const POWER_UP: Duration = Duration::from_millis(20);
/// コマが届かなくなって、これだけたったら消す
const IDLE_OFF: Duration = Duration::from_millis(300);

/// 数珠つなぎの順（DIN 側から n 番目の LED）に、どのキー（マトリクスの 行 × 6 ＋ 列）があるか。
/// 基板（hardware/pcb/skjar.kicad_pcb）の LED のつながりと、各 LED の 4.93 mm 下のスイッチから求めた
const LED_ORDER: [u8; LEDS] = [
    0, 1, 2, 3, 4, 5, 11, 10, 9, 8, 7, 6, 19, 20, 21, 22, 23, 17, 16, 15, 14, 13, 12, 24, 25, 26, 27, 28, 29,
    35, 34, 33, 32, 31, 18, 30, 39, 41, 40, 38, 37, 36,
];

/// CM0 から届いた 1 コマ（マトリクスの順、明るさ 0〜15）
static FRAME: Signal<CriticalSectionRawMutex, [u8; LEDS]> = Signal::new();

/// 1 コマを光らせる（cm0_link から呼ぶ）
pub fn show_frame(levels: [u8; LEDS]) {
    FRAME.signal(levels);
}

/// 0〜15 を、見た目の明るさがそろうように 2 乗して 0〜MAX_LEVEL にする
fn level_to_pwm(level: u8) -> u8 {
    let l = level.min(15) as u32;
    (l * l * MAX_LEVEL / (15 * 15)) as u8
}

pub async fn run(mut leds: PioWs2812<'static, PIO0, 0, LEDS, Grb>, mut power: Output<'static>) -> ! {
    let mut on = false;
    let off = [RGB8::default(); LEDS];
    loop {
        match with_timeout(IDLE_OFF, FRAME.wait()).await {
            Ok(levels) => {
                if !on {
                    info!("rgb: on");
                    power.set_high();
                    Timer::after(POWER_UP).await;
                    on = true;
                }
                let mut colors = [RGB8::default(); LEDS];
                for (chain, &key) in LED_ORDER.iter().enumerate() {
                    let v = level_to_pwm(levels[key as usize]);
                    colors[chain] = RGB8::new(v, v, v);
                }
                leds.write(&colors).await;
            }
            Err(_) => {
                if on {
                    leds.write(&off).await;
                    power.set_low();
                    on = false;
                    info!("rgb: off");
                }
            }
        }
    }
}
