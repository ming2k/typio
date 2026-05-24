//! String utilities — migrated from C `utils/string.c`

use std::ffi::{c_char, c_double, c_int, CStr, CString};
use std::slice;

#[no_mangle]
pub extern "C" fn typio_strdup(str: *const c_char) -> *mut c_char {
    if str.is_null() {
        return std::ptr::null_mut();
    }
    unsafe {
        let cstr = CStr::from_ptr(str);
        let len = cstr.to_bytes_with_nul().len();
        let copy = libc::malloc(len) as *mut c_char;
        if !copy.is_null() {
            std::ptr::copy_nonoverlapping(str, copy, len);
        }
        copy
    }
}

#[no_mangle]
pub extern "C" fn typio_strndup(str: *const c_char, n: usize) -> *mut c_char {
    if str.is_null() {
        return std::ptr::null_mut();
    }
    unsafe {
        let bytes = slice::from_raw_parts(str as *const u8, n);
        let len = bytes.iter().position(|&b| b == 0).unwrap_or(n);
        let copy = libc::malloc(len + 1) as *mut c_char;
        if !copy.is_null() {
            std::ptr::copy_nonoverlapping(str, copy, len);
            *copy.add(len) = 0;
        }
        copy
    }
}

#[no_mangle]
pub extern "C" fn typio_strjoin(a: *const c_char, b: *const c_char) -> *mut c_char {
    if a.is_null() && b.is_null() {
        return std::ptr::null_mut();
    }
    if a.is_null() {
        return typio_strdup(b);
    }
    if b.is_null() {
        return typio_strdup(a);
    }
    unsafe {
        let a_bytes = CStr::from_ptr(a).to_bytes();
        let b_bytes = CStr::from_ptr(b).to_bytes();
        let mut result = Vec::with_capacity(a_bytes.len() + b_bytes.len() + 1);
        result.extend_from_slice(a_bytes);
        result.extend_from_slice(b_bytes);
        result.push(0);
        let ptr = libc::malloc(result.len()) as *mut c_char;
        if !ptr.is_null() {
            std::ptr::copy_nonoverlapping(result.as_ptr(), ptr as *mut u8, result.len());
        }
        ptr
    }
}

#[no_mangle]
pub extern "C" fn typio_strjoin3(
    a: *const c_char,
    b: *const c_char,
    c: *const c_char,
) -> *mut c_char {
    let ab = typio_strjoin(a, b);
    if ab.is_null() {
        return std::ptr::null_mut();
    }
    let result = typio_strjoin(ab, c);
    unsafe { libc::free(ab as *mut libc::c_void) };
    result
}

#[no_mangle]
pub extern "C" fn typio_path_join(base: *const c_char, suffix: *const c_char) -> *mut c_char {
    if base.is_null() || suffix.is_null() {
        return std::ptr::null_mut();
    }
    unsafe {
        let base_str = CStr::from_ptr(base).to_str().unwrap_or("");
        let suffix_str = CStr::from_ptr(suffix).to_str().unwrap_or("");
        let need_slash = !base_str.is_empty() && !base_str.ends_with('/');
        let joined = if need_slash {
            format!("{}/{}", base_str, suffix_str)
        } else {
            format!("{}{}", base_str, suffix_str)
        };
        match CString::new(joined) {
            Ok(cstr) => {
                let ptr = cstr.into_raw();
                ptr as *mut c_char
            }
            Err(_) => std::ptr::null_mut(),
        }
    }
}

#[no_mangle]
pub extern "C" fn typio_str_starts_with(str: *const c_char, prefix: *const c_char) -> bool {
    if str.is_null() || prefix.is_null() {
        return false;
    }
    unsafe {
        let s = CStr::from_ptr(str).to_bytes();
        let p = CStr::from_ptr(prefix).to_bytes();
        s.starts_with(p)
    }
}

#[no_mangle]
pub extern "C" fn typio_str_ends_with(str: *const c_char, suffix: *const c_char) -> bool {
    if str.is_null() || suffix.is_null() {
        return false;
    }
    unsafe {
        let s = CStr::from_ptr(str).to_bytes();
        let suf = CStr::from_ptr(suffix).to_bytes();
        s.ends_with(suf)
    }
}

#[no_mangle]
pub extern "C" fn typio_str_equals(a: *const c_char, b: *const c_char) -> bool {
    if a == b {
        return true;
    }
    if a.is_null() || b.is_null() {
        return false;
    }
    unsafe {
        CStr::from_ptr(a).to_bytes() == CStr::from_ptr(b).to_bytes()
    }
}

#[no_mangle]
pub extern "C" fn typio_str_equals_nocase(a: *const c_char, b: *const c_char) -> bool {
    if a == b {
        return true;
    }
    if a.is_null() || b.is_null() {
        return false;
    }
    unsafe {
        let a_str = CStr::from_ptr(a).to_str().unwrap_or("");
        let b_str = CStr::from_ptr(b).to_str().unwrap_or("");
        a_str.eq_ignore_ascii_case(b_str)
    }
}

#[no_mangle]
pub extern "C" fn typio_str_find(haystack: *const c_char, needle: *const c_char) -> *const c_char {
    if haystack.is_null() || needle.is_null() {
        return std::ptr::null();
    }
    unsafe {
        let h = CStr::from_ptr(haystack).to_str().unwrap_or("");
        let n = CStr::from_ptr(needle).to_str().unwrap_or("");
        match h.find(n) {
            Some(pos) => haystack.add(pos),
            None => std::ptr::null(),
        }
    }
}

#[no_mangle]
pub extern "C" fn typio_str_to_int(str: *const c_char, default_val: c_int) -> c_int {
    if str.is_null() {
        return default_val;
    }
    unsafe {
        let s = CStr::from_ptr(str).to_str().unwrap_or("");
        s.parse::<c_int>().unwrap_or(default_val)
    }
}

#[no_mangle]
pub extern "C" fn typio_str_to_double(str: *const c_char, default_val: c_double) -> c_double {
    if str.is_null() {
        return default_val;
    }
    unsafe {
        let s = CStr::from_ptr(str).to_str().unwrap_or("");
        s.parse::<c_double>().unwrap_or(default_val)
    }
}

#[no_mangle]
pub extern "C" fn typio_str_to_bool(str: *const c_char, default_val: bool) -> bool {
    if str.is_null() {
        return default_val;
    }
    unsafe {
        let s = CStr::from_ptr(str).to_str().unwrap_or("").to_lowercase();
        match s.as_str() {
            "true" | "yes" | "1" | "on" => true,
            "false" | "no" | "0" | "off" => false,
            _ => default_val,
        }
    }
}

/* UTF-8 utilities */

#[no_mangle]
pub extern "C" fn typio_utf8_strlen(str: *const c_char) -> usize {
    if str.is_null() {
        return 0;
    }
    unsafe {
        let bytes = CStr::from_ptr(str).to_bytes();
        bytes.iter().filter(|&&b| (b & 0xC0) != 0x80).count()
    }
}

#[no_mangle]
pub extern "C" fn typio_utf8_next(str: *const c_char) -> *const c_char {
    if str.is_null() || unsafe { *str } == 0 {
        return str;
    }
    unsafe {
        let mut p = str.add(1);
        while *p != 0 && (*p as u8 & 0xC0) == 0x80 {
            p = p.add(1);
        }
        p
    }
}

#[no_mangle]
pub extern "C" fn typio_utf8_prev(str: *const c_char, start: *const c_char) -> *const c_char {
    if str.is_null() || start.is_null() || str <= start {
        return start;
    }
    unsafe {
        let mut p = str.sub(1);
        while p > start && (*p as u8 & 0xC0) == 0x80 {
            p = p.sub(1);
        }
        p
    }
}

#[no_mangle]
pub extern "C" fn typio_utf8_get_char(str: *const c_char) -> u32 {
    if str.is_null() || unsafe { *str } == 0 {
        return 0;
    }
    unsafe {
        let c = *str as u8;
        if c < 0x80 {
            return c as u32;
        }
        let (mut result, remaining) = if (c & 0xE0) == 0xC0 {
            (c as u32 & 0x1F, 1)
        } else if (c & 0xF0) == 0xE0 {
            (c as u32 & 0x0F, 2)
        } else if (c & 0xF8) == 0xF0 {
            (c as u32 & 0x07, 3)
        } else {
            return 0xFFFD;
        };
        let mut p = str.add(1);
        for _ in 0..remaining {
            if *p == 0 || (*p as u8 & 0xC0) != 0x80 {
                return 0xFFFD;
            }
            result = (result << 6) | ((*p as u8) & 0x3F) as u32;
            p = p.add(1);
        }
        result
    }
}

#[no_mangle]
pub extern "C" fn typio_utf8_encode(codepoint: u32, buf: *mut c_char) -> usize {
    if buf.is_null() {
        return 0;
    }
    unsafe {
        if codepoint < 0x80 {
            *buf = codepoint as c_char;
            return 1;
        }
        if codepoint < 0x800 {
            *buf = (0xC0 | (codepoint >> 6)) as c_char;
            *buf.add(1) = (0x80 | (codepoint & 0x3F)) as c_char;
            return 2;
        }
        if codepoint < 0x10000 {
            *buf = (0xE0 | (codepoint >> 12)) as c_char;
            *buf.add(1) = (0x80 | ((codepoint >> 6) & 0x3F)) as c_char;
            *buf.add(2) = (0x80 | (codepoint & 0x3F)) as c_char;
            return 3;
        }
        if codepoint < 0x110000 {
            *buf = (0xF0 | (codepoint >> 18)) as c_char;
            *buf.add(1) = (0x80 | ((codepoint >> 12) & 0x3F)) as c_char;
            *buf.add(2) = (0x80 | ((codepoint >> 6) & 0x3F)) as c_char;
            *buf.add(3) = (0x80 | (codepoint & 0x3F)) as c_char;
            return 4;
        }
        0
    }
}
