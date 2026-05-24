//! C-compatible type definitions matching `include/typio/*.h`

use crate::TypioInputContext;
use std::ffi::{c_char, c_void};
use std::os::raw::{c_double, c_int};

/* -------------------------------------------------------------------------- */
/* Result codes                                                               */
/* -------------------------------------------------------------------------- */

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioResult {
    TypioOk = 0,
    TypioError = -1,
    TypioErrorInvalidArgument = -2,
    TypioErrorOutOfMemory = -3,
    TypioErrorNotFound = -4,
    TypioErrorAlreadyExists = -5,
    TypioErrorNotInitialized = -6,
    TypioErrorEngineLoadFailed = -7,
    TypioErrorEngineNotAvailable = -8,
}

/* -------------------------------------------------------------------------- */
/* Config types                                                               */
/* -------------------------------------------------------------------------- */

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioConfigType {
    TypioConfigString = 0,
    TypioConfigInt = 1,
    TypioConfigBool = 2,
    TypioConfigFloat = 3,
    TypioConfigArray = 4,
    TypioConfigObject = 5,
}

#[repr(C)]
pub union TypioConfigValueData {
    pub string_val: *mut c_char,
    pub int_val: c_int,
    pub bool_val: bool,
    pub float_val: c_double,
    pub array_val: TypioArray,
    pub object_val: *mut crate::config::Config,
}

impl Clone for TypioConfigValueData {
    fn clone(&self) -> Self {
        // Union clone is unsafe; callers must know the active variant.
        // This impl exists only to satisfy derive requirements on enclosing structs.
        unsafe { std::ptr::read(self) }
    }
}

impl Copy for TypioConfigValueData {}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct TypioArray {
    pub items: *mut TypioConfigValue,
    pub count: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct TypioConfigValue {
    pub type_: TypioConfigType,
    pub data: TypioConfigValueData,
}

/* -------------------------------------------------------------------------- */
/* Schema types                                                               */
/* -------------------------------------------------------------------------- */

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioFieldType {
    TypioFieldString = 0,
    TypioFieldInt = 1,
    TypioFieldBool = 2,
    TypioFieldFloat = 3,
}

#[repr(C)]
pub union TypioFieldDefault {
    pub s: *const c_char,
    pub i: c_int,
    pub b: bool,
    pub f: c_double,
}

impl Clone for TypioFieldDefault {
    fn clone(&self) -> Self {
        unsafe { std::ptr::read(self) }
    }
}

impl Copy for TypioFieldDefault {}

#[repr(C)]
pub struct TypioConfigField {
    pub key: *const c_char,
    pub type_: TypioFieldType,
    pub def: TypioFieldDefault,
    pub ui_label: *const c_char,
    pub ui_section: *const c_char,
    pub ui_min: c_int,
    pub ui_max: c_int,
    pub ui_step: c_int,
    pub ui_options: *const *const c_char,
    pub runtime_property: *const c_char,
}

/* -------------------------------------------------------------------------- */
/* Engine types                                                               */
/* -------------------------------------------------------------------------- */

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioEngineType {
    TypioEngineTypeKeyboard = 0,
    TypioEngineTypeVoice = 1,
    TypioEngineTypeHandwriting = 2,
    TypioEngineTypeCustom = 100,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioEngineCapability {
    TypioCapNone = 0,
    TypioCapPreedit = 1 << 0,
    TypioCapCandidates = 1 << 1,
    TypioCapPrediction = 1 << 2,
    TypioCapVoiceInput = 1 << 3,
    TypioCapContinuousVoice = 1 << 4,
    TypioCapPunctuation = 1 << 5,
    TypioCapLearning = 1 << 6,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioModeClass {
    TypioModeClassNative = 0,
    TypioModeClassLatin = 1,
}

#[repr(C)]
pub struct TypioEngineMode {
    pub mode_class: TypioModeClass,
    pub mode_id: *const c_char,
    pub display_label: *const c_char,
    pub icon_name: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioKeyProcessResult {
    TypioKeyNotHandled = 0,
    TypioKeyHandled = 1,
    TypioKeyComposing = 2,
    TypioKeyCommitted = 3,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioEventType {
    TypioEventKeyPress = 0,
    TypioEventKeyRelease = 1,
    TypioEventFocusIn = 2,
    TypioEventFocusOut = 3,
    TypioEventReset = 4,
    TypioEventVoiceStart = 5,
    TypioEventVoiceEnd = 6,
    TypioEventVoiceData = 7,
    TypioEventCommit = 8,
    TypioEventCandidateSelect = 9,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioModifier {
    TypioModNone = 0,
    TypioModShift = 1 << 0,
    TypioModCtrl = 1 << 1,
    TypioModAlt = 1 << 2,
    TypioModSuper = 1 << 3,
    TypioModCapslock = 1 << 4,
    TypioModNumlock = 1 << 5,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct TypioKeyEvent {
    pub type_: TypioEventType,
    pub keycode: u32,
    pub keysym: u32,
    pub modifiers: u32,
    pub unicode: u32,
    pub time: u64,
    pub is_repeat: bool,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct TypioVoiceEvent {
    pub type_: TypioEventType,
    pub audio_data: *const c_void,
    pub audio_size: usize,
    pub sample_rate: c_int,
    pub channels: c_int,
    pub bits_per_sample: c_int,
}

#[repr(C)]
pub struct TypioEvent {
    pub type_: TypioEventType,
    pub time: u64,
    pub data: TypioEventData,
}

impl Clone for TypioEvent {
    fn clone(&self) -> Self {
        Self {
            type_: self.type_,
            time: self.time,
            data: self.data.clone(),
        }
    }
}

impl Copy for TypioEvent {}

#[repr(C)]
#[derive(Copy)]
pub union TypioEventData {
    pub key: TypioKeyEvent,
    pub voice: TypioVoiceEvent,
}

impl Clone for TypioEventData {
    fn clone(&self) -> Self {
        *self
    }
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioLogLevel {
    TypioLogDebug = 0,
    TypioLogInfo = 1,
    TypioLogWarning = 2,
    TypioLogError = 3,
}

#[repr(C)]
pub struct TypioEngineInfo {
    pub name: *const c_char,
    pub display_name: *const c_char,
    pub description: *const c_char,
    pub version: *const c_char,
    pub author: *const c_char,
    pub icon: *const c_char,
    pub language: *const c_char,
    pub type_: TypioEngineType,
    pub capabilities: u32,
    pub api_version: c_int,
    pub struct_size: usize,
}

use crate::TypioInstance;

#[repr(C)]
pub struct TypioEngine {
    pub info: *const TypioEngineInfo,
    pub base_ops: *const TypioEngineBaseOps,
    pub keyboard: *const TypioKeyboardEngineOps,
    pub voice: *const TypioVoiceEngineOps,
    pub instance: *mut TypioInstance,
    pub user_data: *mut std::ffi::c_void,
    pub active: bool,
    pub initialized: bool,
    pub config_path: *mut c_char,
}

pub type TypioEngineFactory = unsafe extern "C" fn() -> *mut TypioEngine;
pub type TypioEngineInfoFunc = unsafe extern "C" fn() -> *const TypioEngineInfo;

#[repr(C)]
pub struct TypioEngineBaseOps {
    pub init: Option<extern "C" fn(*mut TypioEngine, *mut TypioInstance) -> TypioResult>,
    pub destroy: Option<extern "C" fn(*mut TypioEngine)>,
    pub deactivate: Option<extern "C" fn(*mut TypioEngine)>,
    pub focus_in: Option<extern "C" fn(*mut TypioEngine, *mut TypioInputContext)>,
    pub focus_out: Option<extern "C" fn(*mut TypioEngine, *mut TypioInputContext)>,
    pub reset: Option<extern "C" fn(*mut TypioEngine, *mut TypioInputContext)>,
    pub reload_config: Option<extern "C" fn(*mut TypioEngine) -> TypioResult>,
}

#[repr(C)]
pub struct TypioKeyboardEngineOps {
    pub process_key: Option<extern "C" fn(*mut TypioEngine, *mut TypioInputContext, *const std::ffi::c_void) -> TypioKeyProcessResult>,
    pub get_mode: Option<extern "C" fn(*mut TypioEngine, *mut TypioInputContext) -> *const TypioEngineMode>,
    pub set_mode: Option<extern "C" fn(*mut TypioEngine, *mut TypioInputContext, *const c_char) -> TypioResult>,
}

#[repr(C)]
pub struct TypioVoiceEngineOps {
    pub is_ready: Option<extern "C" fn(*mut TypioEngine) -> bool>,
    pub process_audio: Option<extern "C" fn(*mut TypioEngine, *const f32, usize) -> *mut c_char>,
}


/* -------------------------------------------------------------------------- */
/* Instance config                                                            */
/* -------------------------------------------------------------------------- */

#[repr(C)]
pub struct TypioInstanceConfig {
    pub config_dir: *const c_char,
    pub data_dir: *const c_char,
    pub state_dir: *const c_char,
    pub engine_dir: *const c_char,
    pub default_engine: *const c_char,
    pub log_callback: Option<TypioLogCallback>,
    pub log_user_data: *mut c_void,
}

/* -------------------------------------------------------------------------- */
/* Callback types                                                             */
/* -------------------------------------------------------------------------- */

pub type TypioEngineChangedCallback = extern "C" fn(*mut TypioInstance, *const TypioEngineInfo, *mut c_void);
pub type TypioVoiceEngineChangedCallback = extern "C" fn(*mut TypioInstance, *const TypioEngineInfo, *mut c_void);
pub type TypioStatusIconChangedCallback = extern "C" fn(*mut TypioInstance, *const c_char, *mut c_void);
pub type TypioModeChangedCallback = extern "C" fn(*mut TypioInstance, *const TypioEngineMode, *mut c_void);
pub type TypioLogCallback = extern "C" fn(TypioLogLevel, *const c_char, *mut c_void);
