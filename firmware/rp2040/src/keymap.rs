//! SKJÁR のキーマップ（試作用の仮のもの）。
//!
//! 0〜6 行目は物理キー。マトリクスの位置は hardware/layout/README.md の「キーマトリクス」に従う
//! （7 行 × 6 列で、左右の半分を別の行に置き、列は外側から COL0 → COL5）。
//!
//! 7〜10 行目は画面のタッチキー（2 段 × 12 キー）。マトリクスにはなく、CM0 から UART で届いた
//! 押した・離したを、RMK にキーの出来事として渡す（cm0_link.rs）。物理キーと同じキーマップに
//! 置くので、レイヤーも Vial も同じように効く。番号 i（0〜23。上の段が 0〜11、下の段が 12〜23、
//! どちらも左から）は 7 + i / 6 行目、i % 6 列目。
//!
//! キーボードモード（タッチ UI を出して PC に打つ）とサイバーデックモード（コンソールで CM0 に打つ）で、
//! キーマップを分ける：
//!   - レイヤー 0〜2：キーボードモード（0 が基本、1 / 2 はスペースの長押し）
//!   - レイヤー 3〜5：サイバーデックモード（3 が基本、4 / 5 はスペースの長押し）
//!
//! モードは「既定のレイヤー」で切り替える。11 行目（どこにもないキー）の 0 列目が df!(0)、1 列目が df!(3) で、
//! RP2040 がこれを押したことにする（cm0_link.rs の set_mode）。既定のレイヤーより下は、レイヤーキーを
//! 押していない限り見ないので、3〜5 は 0〜2 と独立している。どちらも Vial で別々に変えられる。
//!
//! タブを開くキー（Vial の USER0〜3）：USER0 = Keys、USER1 = Pad、USER2 = Media、USER3 = System。

use rmk::types::action::KeyAction;
use rmk::types::modifier::ModifierCombination;
use rmk::{a, df, k, layer, lt, mt, user};

/// 物理キーのマトリクスの行の数
pub const MATRIX_ROW: usize = 7;
/// キーマップの行の数（物理キー 7 行 ＋ タッチキー 4 行 ＋ モードを切り替えるキー 1 行）
pub const ROW: usize = 12;
pub const COL: usize = 6;
pub const NUM_LAYER: usize = 6;

/// タッチキーの数と、キーマップでの位置
pub const TOUCH_KEYS: usize = 24;
pub const TOUCH_ROW0: usize = MATRIX_ROW;

pub const fn touch_key_pos(index: usize) -> (u8, u8) {
    ((TOUCH_ROW0 + index / COL) as u8, (index % COL) as u8)
}

/// モードを切り替えるキーの位置（行、列）
pub const MODE_ROW: u8 = 11;
pub const MODE_KEYBOARD_COL: u8 = 0;
pub const MODE_CYBERDECK_COL: u8 = 1;

#[rustfmt::skip]
pub const fn get_default_keymap() -> [[[KeyAction; COL]; ROW]; NUM_LAYER] {
    [
        // ---- キーボードモード ----

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
            [k!(LCtrl), k!(LGui), k!(LAlt), k!(RAlt), lt!(1, Space), lt!(2, Space)],
            // タッチキーの上の段：F1〜F12
            [k!(F1), k!(F2), k!(F3), k!(F4), k!(F5), k!(F6)],
            [k!(F7), k!(F8), k!(F9), k!(F10), k!(F11), k!(F12)],
            // タッチキーの下の段：Esc 1〜0 Bksp（記号は Shift ＋ 数字で打つ）
            [k!(Escape), k!(Kc1), k!(Kc2), k!(Kc3), k!(Kc4), k!(Kc5)],
            [k!(Kc6), k!(Kc7), k!(Kc8), k!(Kc9), k!(Kc0), k!(Backspace)],
            // モードを切り替えるキー（どこにもない。RP2040 が押す）
            [df!(0), df!(3), a!(No), a!(No), a!(No), a!(No)]
        ]),
        // 1：数字・記号・矢印（左のスペースの長押し）
        layer!([
            [k!(Grave), k!(Kc1), k!(Kc2), k!(Kc3), k!(Kc4), k!(Kc5)],
            [k!(Delete), k!(Kc0), k!(Kc9), k!(Kc8), k!(Kc7), k!(Kc6)],
            [a!(Transparent), k!(Minus), k!(Equal), k!(LeftBracket), k!(RightBracket), k!(Backslash)],
            [a!(Transparent), a!(Transparent), k!(Right), k!(Up), k!(Down), k!(Left)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), k!(Quote), k!(Semicolon), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            // タッチキーの上の段：移動・編集と、あまり使わないキー（PrtSc ScrLk Pause）。10 番目は ↑
            // （下の ↓ の真上）。残りは空き（何もしない）
            [k!(Home), k!(End), k!(PageUp), k!(PageDown), k!(Insert), k!(PrintScreen)],
            [k!(ScrollLock), k!(Pause), a!(No), k!(Up), a!(No), a!(No)],
            // タッチキーの下の段（押しやすい側）：左端は Caps、数字の段にない記号（, . / は物理キーにあり、
            // - は使わないので置かない）、← ↓ →（上の ↑ と逆 T 字）、右端は Del（レイヤー 0 の Bksp と同じ位置）
            [k!(CapsLock), k!(Grave), k!(Equal), k!(LeftBracket), k!(RightBracket), k!(Backslash)],
            [k!(Semicolon), k!(Quote), k!(Left), k!(Down), k!(Right), k!(Delete)],
            [df!(0), df!(3), a!(No), a!(No), a!(No), a!(No)]
        ]),
        // 2：F キー・メディア・タブ（右のスペースの長押し）
        layer!([
            [a!(Transparent), k!(F1), k!(F2), k!(F3), k!(F4), k!(F5)],
            [a!(Transparent), k!(F10), k!(F9), k!(F8), k!(F7), k!(F6)],
            [a!(Transparent), k!(AudioMute), k!(AudioVolDown), k!(AudioVolUp), k!(MediaPlayPause), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), k!(F12), k!(F11)],
            // R4：Z X C V でタブを開く（Keys Pad Media System）
            [a!(Transparent), user!(0), user!(1), user!(2), user!(3), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            // タッチキーの上の段：メディアと明るさ。下の段：タブを開く。
            // ほかは空き（透過にすると、レイヤー 0 の F キーや数字が混ざって見えるため）
            [k!(AudioMute), k!(AudioVolDown), k!(AudioVolUp), k!(MediaPrevTrack), k!(MediaPlayPause), k!(MediaNextTrack)],
            [k!(BrightnessDown), k!(BrightnessUp), a!(No), a!(No), a!(No), a!(No)],
            [user!(0), user!(1), user!(2), user!(3), a!(No), a!(No)],
            [a!(No), a!(No), a!(No), a!(No), a!(No), a!(No)],
            [df!(0), df!(3), a!(No), a!(No), a!(No), a!(No)]
        ]),

        // ---- サイバーデックモード（コンソール。初めはキーボードモードと同じ並び。タッチキーは画面に出ないので空き） ----

        // 3：基本
        layer!([
            [k!(Escape), k!(Q), k!(W), k!(E), k!(R), k!(T)],
            [k!(Backspace), k!(P), k!(O), k!(I), k!(U), k!(Y)],
            [k!(Tab), k!(A), k!(S), k!(D), k!(F), k!(G)],
            [k!(RCtrl), k!(Enter), k!(L), k!(K), k!(J), k!(H)],
            [k!(LShift), k!(Z), k!(X), k!(C), k!(V), k!(B)],
            [k!(RGui), mt!(Slash, ModifierCombination::RSHIFT), k!(Dot), k!(Comma), k!(M), k!(N)],
            // スペースの長押しは、サイバーデックモードのレイヤー 4 / 5
            [k!(LCtrl), k!(LGui), k!(LAlt), k!(RAlt), lt!(4, Space), lt!(5, Space)],
            [a!(No), a!(No), a!(No), a!(No), a!(No), a!(No)],
            [a!(No), a!(No), a!(No), a!(No), a!(No), a!(No)],
            [a!(No), a!(No), a!(No), a!(No), a!(No), a!(No)],
            [a!(No), a!(No), a!(No), a!(No), a!(No), a!(No)],
            [df!(0), df!(3), a!(No), a!(No), a!(No), a!(No)]
        ]),
        // 4：数字・記号・矢印（左のスペースの長押し）
        layer!([
            [k!(Grave), k!(Kc1), k!(Kc2), k!(Kc3), k!(Kc4), k!(Kc5)],
            [k!(Delete), k!(Kc0), k!(Kc9), k!(Kc8), k!(Kc7), k!(Kc6)],
            [a!(Transparent), k!(Minus), k!(Equal), k!(LeftBracket), k!(RightBracket), k!(Backslash)],
            [a!(Transparent), a!(Transparent), k!(Right), k!(Up), k!(Down), k!(Left)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), k!(Quote), k!(Semicolon), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [df!(0), df!(3), a!(No), a!(No), a!(No), a!(No)]
        ]),
        // 5：F キー・メディア（右のスペースの長押し）
        layer!([
            [a!(Transparent), k!(F1), k!(F2), k!(F3), k!(F4), k!(F5)],
            [a!(Transparent), k!(F10), k!(F9), k!(F8), k!(F7), k!(F6)],
            [a!(Transparent), k!(AudioMute), k!(AudioVolDown), k!(AudioVolUp), k!(MediaPlayPause), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), k!(F12), k!(F11)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent), a!(Transparent)],
            [df!(0), df!(3), a!(No), a!(No), a!(No), a!(No)]
        ]),
    ]
}
