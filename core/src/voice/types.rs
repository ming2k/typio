use std::ffi::c_char;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(C)]
pub enum VoiceState {
    Idle = 0,
    Loading = 1,
    Recording = 2,
    Processing = 3,
}

pub enum VoiceEvent {
    StateChange(VoiceState),
    Result(String),
    Error(&'static str),
}

/// C-compatible event structure for callback.
#[repr(C)]
pub struct TypioVoiceSessionEvent {
    pub type_: TypioVoiceSessionEventType,
    pub state: VoiceState,
    pub text: *mut c_char,
    pub error: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioVoiceSessionEventType {
    StateChange = 0,
    Result = 1,
    Error = 2,
}

pub type TypioVoiceSessionEventCallback =
    extern "C" fn(event: *const TypioVoiceSessionEvent, user_data: *mut std::ffi::c_void);
