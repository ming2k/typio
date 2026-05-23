//! C-compatible type definitions matching `include/typio/*.h`

use std::ffi::c_char;
use std::os::raw::{c_double, c_int};

/* -------------------------------------------------------------------------- */
/* Result codes                                                               */
/* -------------------------------------------------------------------------- */

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioResult {
    Ok = 0,
    Error = -1,
    InvalidArgument = -2,
    OutOfMemory = -3,
    NotFound = -4,
    AlreadyExists = -5,
    NotInitialized = -6,
    EngineLoadFailed = -7,
    EngineNotAvailable = -8,
}

/* -------------------------------------------------------------------------- */
/* Config types                                                               */
/* -------------------------------------------------------------------------- */

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioConfigType {
    String = 0,
    Int = 1,
    Bool = 2,
    Float = 3,
    Array = 4,
    Object = 5,
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
    String = 0,
    Int = 1,
    Bool = 2,
    Float = 3,
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
