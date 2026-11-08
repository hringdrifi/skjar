//! SKJÁR のキーマップ（試作用の仮のもの）。
//!
//! マトリクスの位置は hardware/layout/README.md の「キーマトリクス」に従う。
//! 7 行 × 6 列で、左右の半分を別の行に置き、列は外側から COL0 → COL5。

use rmk::types::action::KeyAction;
use rmk::types::modifier::ModifierCombination;
use rmk::{a, k, layer, lt, mt};

pub const ROW: usize = 7;
pub const COL: usize = 6;
pub const NUM_LAYER: usize = 3;

#[rustfmt::skip]
pub const fn get_default_keymap() -> [[[KeyAction; COL]; ROW]; NUM_LAYER] {
    [
        // 0：基本
        layer!([
            // R0：上段の左（Esc Q W E R T）
            [k!(Escape), k!(Q), k!(W), k!(E), k!(R), k!(T)],
            // R1：上段の右（外側から Bksp P O I U Y）
            [k!(Backspace), k!(P), k!(O), k!(I), k!(U), k!(Y)],
            // R2：中段の左（Tab A S D F G）
            [k!(Tab), k!(A), k!(S), k!(D), k!(F), k!(G)],
            // R3：中段の右（RCtrl Enter L K J H）
            [k!(RCtrl), k!(Enter), k!(L), k!(K), k!(J), k!(H)],
            // R4：下段の左（LShift Z X C V B）
            [k!(LShift), k!(Z), k!(X), k!(C), k!(V), k!(B)],
            // R5：下段の右（RGui /(長押しで Shift) . , M N）
            [k!(RGui), mt!(Slash, ModifierCombination::RSHIFT), k!(Dot), k!(Comma), k!(M), k!(N)],
            // R6：最下段（LCtrl LGui LAlt RAlt Space/L1 Space/L2）
            [k!(LCtrl), k!(LGui), k!(LAlt), k!(RAlt), lt!(1, Space), lt!(2, Space)]
        ]),
        // 1：数字・記号・矢印
        layer!([
            [k!(Grave), k!(Kc1), k!(Kc2), k!(Kc3), k!(Kc4), k!(Kc5)],
            [k!(Delete), k!(Kc0), k!(Kc9), k!(Kc8), k!(Kc7), k!(Kc6)],
            [a!(Transparent), k!(Minus), k!(Equal), k!(LeftBracket), k!(RightBracket), k!(Backslash)],
            [a!(Transparent), a!(Transparent), k!(Right), k!(Up), k!(Down), k!(Left)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), k!(Quote), k!(Semicolon), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)]
        ]),
        // 2：F キー・メディア（Consumer Control のレポートの確認用）
        layer!([
            [a!(Transparent), k!(F1), k!(F2), k!(F3), k!(F4), k!(F5)],
            [a!(Transparent), k!(F10), k!(F9), k!(F8), k!(F7), k!(F6)],
            [a!(Transparent), k!(AudioMute), k!(AudioVolDown), k!(AudioVolUp), k!(MediaPlayPause), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), k!(F12), k!(F11)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)]
        ]),
    ]
}
