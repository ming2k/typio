pub mod backend;
pub mod engine;
pub mod proxy;
pub mod session;
pub mod types;

pub use types::{VoiceEvent, VoiceState};

use crate::types::TypioLogLevel;
use std::ffi::CString;

pub(crate) fn log_msg(level: TypioLogLevel, msg: &str) {
    if let Ok(c_msg) = CString::new(msg) {
        crate::log::_typio_log(level, c_msg.as_ptr());
    }
}
