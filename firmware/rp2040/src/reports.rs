//! 物理キー（RMK）とタッチキー（CM0 から UART で届く）のレポートを合わせて、出力先に送る。
//!
//! キーボードのレポートは、両方の修飾キーの OR と、両方の押されているキー（合わせて 6 個まで）。
//! Consumer は、どちらか押されている方（両方なら物理キーを優先）。
//! マウス（RMK のマウスキーと、CM0 のトラックパッド）は、動きはそのまま送り、ボタンは両方の OR。
//!
//! 出力先はモードで決まる（docs/protocol.md の 2.6）：キーボードモードは PC（USB HID、[`usb_hid`]）、
//! サイバーデックモードは CM0（HID over I²C、[`hid_i2c`]）。出力先が変わったら、元の出力先に
//! 「全部離した」を送ってから、新しい出力先に今の状態を送り直す（押したままで残らないように）。

use core::cell::RefCell;

use embassy_sync::blocking_mutex::Mutex;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use log::{debug, info, warn};

use crate::cm0_link;
use crate::hid_i2c::{self, InputReport};
use crate::usb_hid::{self, UsbReport};

/// 出力先に送る。to_pc なら PC（USB）、そうでなければ CM0（I²C）
fn send_keyboard(to_pc: bool, modifier: u8, keys: [u8; MAX_KEYS]) {
    if to_pc {
        usb_hid::push(UsbReport::Keyboard { modifier, keys });
    } else {
        hid_i2c::push_input(InputReport::keyboard(modifier, keys));
    }
}

fn send_consumer(to_pc: bool, usage: u16) {
    if to_pc {
        usb_hid::push(UsbReport::Consumer(usage));
    } else {
        hid_i2c::push_input(InputReport::consumer(usage));
    }
}

fn send_mouse(to_pc: bool, buttons: u8, x: i8, y: i8, wheel: i8, pan: i8) {
    if to_pc {
        usb_hid::push(UsbReport::Mouse { buttons, x, y, wheel, pan });
    } else {
        hid_i2c::push_input(InputReport::mouse(buttons, x, y, wheel, pan));
    }
}

/// 出力先がモードと合っているか確かめ、変わっていたら、元の出力先で全部離し、送ったものを忘れる
/// （このあと emit が新しい出力先に今の状態を送る）
fn sync_target(s: &mut State) {
    let to_pc = cm0_link::output_to_pc();
    if to_pc == s.to_pc {
        return;
    }
    info!("reports: output to {}", if to_pc { "the PC (USB)" } else { "CM0 (I2C)" });
    if s.sent.modifier != 0 || s.sent.keys.iter().any(|k| *k != 0) {
        send_keyboard(s.to_pc, 0, [0; MAX_KEYS]);
    }
    if s.sent_consumer != 0 {
        send_consumer(s.to_pc, 0);
    }
    if s.sent_mouse_buttons != 0 {
        send_mouse(s.to_pc, 0, 0, 0, 0, 0);
    }
    s.sent = KeyState::default();
    s.sent_consumer = 0;
    s.sent_mouse_buttons = 0;
    s.to_pc = to_pc;
}

const MAX_KEYS: usize = 6;

#[derive(Clone, Copy, Default)]
struct KeyState {
    modifier: u8,
    keys: [u8; MAX_KEYS],
}

impl KeyState {
    fn press(&mut self, usage: u8) -> bool {
        if self.keys.contains(&usage) {
            return false;
        }
        match self.keys.iter_mut().find(|k| **k == 0) {
            Some(slot) => {
                *slot = usage;
                true
            }
            None => {
                warn!("reports: more than {} touch keys, ignoring 0x{:02X}", MAX_KEYS, usage);
                false
            }
        }
    }

    fn release(&mut self, usage: u8) -> bool {
        let mut changed = false;
        for k in self.keys.iter_mut().filter(|k| **k == usage) {
            *k = 0;
            changed = true;
        }
        changed
    }
}

struct State {
    rmk: KeyState,
    rmk_consumer: u16,
    touch: KeyState,
    touch_consumer: u16,
    /// 最後に送ったもの（同じものは送り直さない）
    sent: KeyState,
    sent_consumer: u16,
    rmk_mouse_buttons: u8,
    touch_mouse_buttons: u8,
    sent_mouse_buttons: u8,
    /// 送り先（true：PC、false：CM0）。sent の中身は、この送り先に送ったもの
    to_pc: bool,
}

static STATE: Mutex<CriticalSectionRawMutex, RefCell<State>> = Mutex::new(RefCell::new(State {
    rmk: KeyState { modifier: 0, keys: [0; MAX_KEYS] },
    rmk_consumer: 0,
    touch: KeyState { modifier: 0, keys: [0; MAX_KEYS] },
    touch_consumer: 0,
    sent: KeyState { modifier: 0, keys: [0; MAX_KEYS] },
    sent_consumer: 0,
    rmk_mouse_buttons: 0,
    touch_mouse_buttons: 0,
    sent_mouse_buttons: 0,
    to_pc: false,
}));

fn with_state<R>(f: impl FnOnce(&mut State) -> R) -> R {
    STATE.lock(|s| f(&mut s.borrow_mut()))
}

/// 両方を合わせて、変わっていれば出力先に送る。
fn emit(s: &mut State) {
    sync_target(s);
    let mut merged = KeyState {
        modifier: s.rmk.modifier | s.touch.modifier,
        keys: s.rmk.keys,
    };
    for &k in s.touch.keys.iter().filter(|k| **k != 0) {
        if !merged.keys.contains(&k)
            && let Some(slot) = merged.keys.iter_mut().find(|x| **x == 0)
        {
            *slot = k;
        }
    }
    if merged.modifier != s.sent.modifier || merged.keys != s.sent.keys {
        s.sent = merged;
        send_keyboard(s.to_pc, merged.modifier, merged.keys);
    }

    let consumer = if s.rmk_consumer != 0 { s.rmk_consumer } else { s.touch_consumer };
    if consumer != s.sent_consumer {
        s.sent_consumer = consumer;
        send_consumer(s.to_pc, consumer);
    }
}

/// CM0 のカーネルが生きているかを確かめるため、I²C に今と同じ内容のキーボードのレポートを置く
/// （cm0_link.rs）。キーの状態は変わらないので、Linux では何も起きない。読みに来るかだけを見る。
/// まだ読まれていないものがあれば置かない（読みに来ない間に積み上がらないように）
pub fn probe_i2c() {
    if hid_i2c::pending() {
        return;
    }
    with_state(|s| {
        let (modifier, keys) = if s.to_pc { (0, [0; MAX_KEYS]) } else { (s.sent.modifier, s.sent.keys) };
        hid_i2c::push_input(InputReport::keyboard(modifier, keys));
    });
}

/// モードが変わった（cm0_link）：出力先を切り替え、今の状態を新しい出力先に送り直す
pub fn retarget() {
    with_state(|s| {
        emit(s);
        emit_mouse(s, 0, 0, 0, 0);
    });
}

/// RMK のキーボードのレポート（その時点の全部の状態）。
pub fn rmk_keyboard(modifier: u8, keys: [u8; MAX_KEYS]) {
    with_state(|s| {
        s.rmk = KeyState { modifier, keys };
        emit(s);
    });
}

/// RMK の Consumer のレポート。
pub fn rmk_consumer(usage: u16) {
    with_state(|s| {
        s.rmk_consumer = usage;
        emit(s);
    });
}

/// マウスのレポートを出す。動きがなくても、ボタンが変わったときは出す。
fn emit_mouse(s: &mut State, x: i8, y: i8, wheel: i8, pan: i8) {
    sync_target(s);
    let buttons = s.rmk_mouse_buttons | s.touch_mouse_buttons;
    if x == 0 && y == 0 && wheel == 0 && pan == 0 && buttons == s.sent_mouse_buttons {
        return;
    }
    s.sent_mouse_buttons = buttons;
    send_mouse(s.to_pc, buttons, x, y, wheel, pan);
}

/// RMK のマウスのレポート（マウスキー）。
pub fn rmk_mouse(buttons: u8, x: i8, y: i8, wheel: i8, pan: i8) {
    with_state(|s| {
        s.rmk_mouse_buttons = buttons;
        emit_mouse(s, x, y, wheel, pan);
    });
}

/// トラックパッドの動きとボタン（UART の PTR）。
pub fn touch_pointer(dx: i8, dy: i8, buttons: u8) {
    with_state(|s| {
        s.touch_mouse_buttons = buttons & 0x07;
        emit_mouse(s, dx, dy, 0, 0);
    });
}

/// トラックパッドのスクロール（UART の WHEEL）。
pub fn touch_wheel(v: i8, h: i8) {
    with_state(|s| emit_mouse(s, 0, 0, v, h));
}

/// タッチキーを押した・離した（UART の KEY。page は 0x07 か 0x0C）。
pub fn touch_key(page: u8, usage: u16, down: bool) {
    with_state(|s| {
        let changed = match page {
            0x07 if (0xE0..=0xE7).contains(&usage) => {
                let bit = 1u8 << (usage - 0xE0);
                let before = s.touch.modifier;
                if down {
                    s.touch.modifier |= bit;
                } else {
                    s.touch.modifier &= !bit;
                }
                before != s.touch.modifier
            }
            0x07 if usage <= 0xFF && usage != 0 => {
                if down {
                    s.touch.press(usage as u8)
                } else {
                    s.touch.release(usage as u8)
                }
            }
            0x0C => {
                let before = s.touch_consumer;
                if down {
                    s.touch_consumer = usage;
                } else if s.touch_consumer == usage {
                    s.touch_consumer = 0;
                }
                before != s.touch_consumer
            }
            _ => {
                warn!("reports: unsupported touch key page 0x{:02X} usage 0x{:X}", page, usage);
                false
            }
        };
        if changed {
            emit(s);
        }
    });
}

/// タッチキーとトラックパッドのボタンをすべて離す（UI の起動、UART の途絶）。
pub fn release_touch() {
    with_state(|s| {
        if s.touch.modifier != 0 || s.touch.keys.iter().any(|k| *k != 0) || s.touch_consumer != 0 {
            debug!("reports: releasing all touch keys");
            s.touch = KeyState::default();
            s.touch_consumer = 0;
            emit(s);
        }
        if s.touch_mouse_buttons != 0 {
            s.touch_mouse_buttons = 0;
            emit_mouse(s, 0, 0, 0, 0);
        }
    });
}
