use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;
use crate::types::*;

pub enum TypioInstance {} // Opaque struct
pub enum TypioKeyEvent {} // Opaque struct

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypioPreeditFormat {
    TypioPreeditNone = 0,
    TypioPreeditUnderline = 1 << 0,
    TypioPreeditHighlight = 1 << 1,
    TypioPreeditBold = 1 << 2,
    TypioPreeditItalic = 1 << 3,
}

#[repr(C)]
pub struct TypioPreeditSegment {
    pub text: *const c_char,
    pub format: u32,
}

#[repr(C)]
pub struct TypioPreedit {
    pub segments: *mut TypioPreeditSegment,
    pub segment_count: usize,
    pub cursor_pos: i32,
}

#[repr(C)]
pub struct TypioCandidate {
    pub text: *const c_char,
    pub comment: *const c_char,
    pub label: *const c_char,
}

#[repr(C)]
pub struct TypioCandidateList {
    pub candidates: *mut TypioCandidate,
    pub count: usize,
    pub page: i32,
    pub page_size: i32,
    pub total: i32,
    pub selected: i32,
    pub has_prev: bool,
    pub has_next: bool,
    pub content_signature: u64,
}

pub type TypioCommitCallback = extern "C" fn(*mut TypioInputContext, *const c_char, *mut c_void);
pub type TypioPreeditCallback = extern "C" fn(*mut TypioInputContext, *const TypioPreedit, *mut c_void);
pub type TypioCandidateCallback = extern "C" fn(*mut TypioInputContext, *const TypioCandidateList, *mut c_void);

extern "C" {
    // External C dependencies needed to hook into the rest of the Typio core
    fn typio_instance_set_focused_context(instance: *mut TypioInstance, ctx: *mut TypioInputContext);
    
    // We need to fetch the engine to forward focus/reset/key events
    fn typio_instance_get_engine_manager(instance: *mut TypioInstance) -> *mut c_void;
    fn typio_engine_manager_get_active(manager: *mut c_void) -> *mut c_void;
    
    // These calls reach into the engine abstraction
    fn _typio_engine_base_focus_in(engine: *mut c_void, ctx: *mut TypioInputContext);
    fn _typio_engine_base_focus_out(engine: *mut c_void, ctx: *mut TypioInputContext);
    fn _typio_engine_base_reset(engine: *mut c_void, ctx: *mut TypioInputContext);
    fn _typio_engine_keyboard_process_key(engine: *mut c_void, ctx: *mut TypioInputContext, event: *const TypioKeyEvent) -> u32; // TypioKeyProcessResult
}

struct PropertyEntry {
    key: String,
    value: *mut c_void,
    free_func: Option<extern "C" fn(*mut c_void)>,
}

pub struct TypioInputContext {
    instance: *mut TypioInstance,
    focused: bool,
    capabilities: u32,
    
    preedit: TypioPreedit,
    preedit_segments: Vec<TypioPreeditSegment>,
    
    candidates: TypioCandidateList,
    candidate_items: Vec<TypioCandidate>,
    
    surrounding_text: Option<CString>,
    surrounding_cursor: i32,
    surrounding_anchor: i32,

    commit_callback: Option<TypioCommitCallback>,
    commit_user_data: *mut c_void,
    preedit_callback: Option<TypioPreeditCallback>,
    preedit_user_data: *mut c_void,
    candidate_callback: Option<TypioCandidateCallback>,
    candidate_user_data: *mut c_void,

    user_data: *mut c_void,
    properties: Vec<PropertyEntry>,
}

impl TypioInputContext {
    fn new(instance: *mut TypioInstance) -> Self {
        TypioInputContext {
            instance,
            focused: false,
            capabilities: 0,
            
            preedit: TypioPreedit {
                segments: ptr::null_mut(),
                segment_count: 0,
                cursor_pos: 0,
            },
            preedit_segments: Vec::new(),
            
            candidates: TypioCandidateList {
                candidates: ptr::null_mut(),
                count: 0,
                page: 0,
                page_size: 10,
                total: 0,
                selected: -1,
                has_prev: false,
                has_next: false,
                content_signature: 0,
            },
            candidate_items: Vec::new(),
            
            surrounding_text: None,
            surrounding_cursor: 0,
            surrounding_anchor: 0,

            commit_callback: None,
            commit_user_data: ptr::null_mut(),
            preedit_callback: None,
            preedit_user_data: ptr::null_mut(),
            candidate_callback: None,
            candidate_user_data: ptr::null_mut(),

            user_data: ptr::null_mut(),
            properties: Vec::new(),
        }
    }
    
    fn clear_preedit_silent(&mut self) {
        for seg in &self.preedit_segments {
            if !seg.text.is_null() {
                unsafe { drop(CString::from_raw(seg.text as *mut c_char)) };
            }
        }
        self.preedit_segments.clear();
        self.preedit.segment_count = 0;
        self.preedit.cursor_pos = 0;
        self.preedit.segments = ptr::null_mut();
    }
    
    fn clear_candidates_silent(&mut self) {
        for cand in &self.candidate_items {
            if !cand.text.is_null() { unsafe { drop(CString::from_raw(cand.text as *mut c_char)) }; }
            if !cand.comment.is_null() { unsafe { drop(CString::from_raw(cand.comment as *mut c_char)) }; }
            if !cand.label.is_null() { unsafe { drop(CString::from_raw(cand.label as *mut c_char)) }; }
        }
        self.candidate_items.clear();
        self.candidates.count = 0;
        self.candidates.page = 0;
        self.candidates.total = 0;
        self.candidates.selected = -1;
        self.candidates.has_prev = false;
        self.candidates.has_next = false;
        self.candidates.content_signature = 0;
        self.candidates.candidates = ptr::null_mut();
    }
}

impl Drop for TypioInputContext {
    fn drop(&mut self) {
        for prop in &self.properties {
            if let Some(free_fn) = prop.free_func {
                if !prop.value.is_null() {
                    free_fn(prop.value);
                }
            }
        }
        self.properties.clear();
        self.clear_preedit_silent();
        self.clear_candidates_silent();
    }
}

const TYPIO_CANDIDATE_SIGNATURE_OFFSET: u64 = 1469598103934665603;
const TYPIO_CANDIDATE_SIGNATURE_PRIME: u64 = 1099511628211;

fn signature_bytes(mut hash: u64, bytes: &[u8]) -> u64 {
    for &b in bytes {
        hash ^= b as u64;
        hash = hash.wrapping_mul(TYPIO_CANDIDATE_SIGNATURE_PRIME);
    }
    hash
}

fn signature_string(mut hash: u64, text: *const c_char) -> u64 {
    let bytes = if text.is_null() {
        b"".as_slice()
    } else {
        unsafe { CStr::from_ptr(text) }.to_bytes()
    };
    hash = signature_bytes(hash, bytes);
    hash ^= 0xff;
    hash = hash.wrapping_mul(TYPIO_CANDIDATE_SIGNATURE_PRIME);
    hash
}

fn candidate_signature(list: &TypioCandidateList) -> u64 {
    let mut hash = TYPIO_CANDIDATE_SIGNATURE_OFFSET;
    
    hash = signature_bytes(hash, &list.count.to_ne_bytes());
    hash = signature_bytes(hash, &list.page.to_ne_bytes());
    hash = signature_bytes(hash, &list.page_size.to_ne_bytes());
    hash = signature_bytes(hash, &list.total.to_ne_bytes());
    hash = signature_bytes(hash, &[list.has_prev as u8]);
    hash = signature_bytes(hash, &[list.has_next as u8]);

    let slice = if list.count > 0 && !list.candidates.is_null() {
        unsafe { std::slice::from_raw_parts(list.candidates, list.count) }
    } else {
        &[]
    };

    for cand in slice {
        hash = signature_string(hash, cand.text);
        hash = signature_string(hash, cand.comment);
        hash = signature_string(hash, cand.label);
    }
    hash
}

#[no_mangle]
pub extern "C" fn typio_input_context_new(instance: *mut TypioInstance) -> *mut TypioInputContext {
    let ctx = Box::new(TypioInputContext::new(instance));
    Box::into_raw(ctx)
}

#[no_mangle]
pub extern "C" fn typio_input_context_free(ctx: *mut TypioInputContext) {
    if !ctx.is_null() {
        unsafe { drop(Box::from_raw(ctx)) };
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_focus_in(ctx: *mut TypioInputContext) {
    if ctx.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    
    if ctx_ref.focused { return; }
    ctx_ref.focused = true;
    
    unsafe {
        typio_instance_set_focused_context(ctx_ref.instance, ctx);
        let manager = typio_instance_get_engine_manager(ctx_ref.instance);
        let engine = typio_engine_manager_get_active(manager);
        if !engine.is_null() {
            _typio_engine_base_focus_in(engine, ctx);
        }
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_focus_out(ctx: *mut TypioInputContext) {
    if ctx.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    
    if !ctx_ref.focused { return; }
    
    unsafe {
        let manager = typio_instance_get_engine_manager(ctx_ref.instance);
        let engine = typio_engine_manager_get_active(manager);
        if !engine.is_null() {
            _typio_engine_base_focus_out(engine, ctx);
        }
        typio_instance_set_focused_context(ctx_ref.instance, ptr::null_mut());
    }
    
    ctx_ref.focused = false;
}

#[no_mangle]
pub extern "C" fn typio_input_context_is_focused(ctx: *mut TypioInputContext) -> bool {
    if ctx.is_null() { return false; }
    unsafe { (*ctx).focused }
}

#[no_mangle]
pub extern "C" fn typio_input_context_reset(ctx: *mut TypioInputContext) {
    if ctx.is_null() { return; }
    
    typio_input_context_clear_preedit(ctx);
    typio_input_context_clear_candidates(ctx);
    
    unsafe {
        let manager = typio_instance_get_engine_manager((*ctx).instance);
        let engine = typio_engine_manager_get_active(manager);
        if !engine.is_null() {
            _typio_engine_base_reset(engine, ctx);
        }
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_process_key(ctx: *mut TypioInputContext, event: *const TypioKeyEvent) -> bool {
    if ctx.is_null() || event.is_null() { return false; }
    
    let result = unsafe {
        let manager = typio_instance_get_engine_manager((*ctx).instance);
        let engine = typio_engine_manager_get_active(manager);
        if engine.is_null() {
            0 // TYPIO_KEY_NOT_HANDLED
        } else {
            _typio_engine_keyboard_process_key(engine, ctx, event)
        }
    };
    
    result != 0
}

#[no_mangle]
pub extern "C" fn typio_input_context_commit(ctx: *mut TypioInputContext, text: *const c_char) {
    if ctx.is_null() || text.is_null() { return; }
    
    typio_input_context_clear_preedit(ctx);
    typio_input_context_clear_candidates(ctx);
    
    let ctx_ref = unsafe { &mut *ctx };
    if let Some(cb) = ctx_ref.commit_callback {
        cb(ctx, text, ctx_ref.commit_user_data);
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_preedit(ctx: *mut TypioInputContext, preedit: *const TypioPreedit) {
    if ctx.is_null() || preedit.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    let p_ref = unsafe { &*preedit };
    
    ctx_ref.clear_preedit_silent();
    
    let slice = if p_ref.segment_count > 0 && !p_ref.segments.is_null() {
        unsafe { std::slice::from_raw_parts(p_ref.segments, p_ref.segment_count) }
    } else {
        &[]
    };
    
    for seg in slice {
        let text_copy = if seg.text.is_null() {
            ptr::null()
        } else {
            unsafe { CStr::from_ptr(seg.text) }.to_owned().into_raw() as *const c_char
        };
        ctx_ref.preedit_segments.push(TypioPreeditSegment {
            text: text_copy,
            format: seg.format,
        });
    }
    
    ctx_ref.preedit.segment_count = p_ref.segment_count;
    ctx_ref.preedit.cursor_pos = p_ref.cursor_pos;
    ctx_ref.preedit.segments = ctx_ref.preedit_segments.as_mut_ptr();
    
    if let Some(cb) = ctx_ref.preedit_callback {
        cb(ctx, &ctx_ref.preedit, ctx_ref.preedit_user_data);
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_get_preedit(ctx: *mut TypioInputContext) -> *const TypioPreedit {
    if ctx.is_null() { return ptr::null(); }
    unsafe { &(*ctx).preedit }
}

#[no_mangle]
pub extern "C" fn typio_input_context_clear_preedit(ctx: *mut TypioInputContext) {
    if ctx.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    
    ctx_ref.clear_preedit_silent();
    
    if let Some(cb) = ctx_ref.preedit_callback {
        cb(ctx, &ctx_ref.preedit, ctx_ref.preedit_user_data);
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_candidates(ctx: *mut TypioInputContext, candidates: *const TypioCandidateList) {
    if ctx.is_null() || candidates.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    let c_ref = unsafe { &*candidates };
    
    ctx_ref.clear_candidates_silent();
    
    let slice = if c_ref.count > 0 && !c_ref.candidates.is_null() {
        unsafe { std::slice::from_raw_parts(c_ref.candidates, c_ref.count) }
    } else {
        &[]
    };
    
    for cand in slice {
        ctx_ref.candidate_items.push(TypioCandidate {
            text: if cand.text.is_null() { ptr::null() } else { unsafe { CStr::from_ptr(cand.text) }.to_owned().into_raw() as *const c_char },
            comment: if cand.comment.is_null() { ptr::null() } else { unsafe { CStr::from_ptr(cand.comment) }.to_owned().into_raw() as *const c_char },
            label: if cand.label.is_null() { ptr::null() } else { unsafe { CStr::from_ptr(cand.label) }.to_owned().into_raw() as *const c_char },
        });
    }
    
    ctx_ref.candidates.count = c_ref.count;
    ctx_ref.candidates.page = c_ref.page;
    ctx_ref.candidates.page_size = c_ref.page_size;
    ctx_ref.candidates.total = c_ref.total;
    ctx_ref.candidates.selected = c_ref.selected;
    ctx_ref.candidates.has_prev = c_ref.has_prev;
    ctx_ref.candidates.has_next = c_ref.has_next;
    ctx_ref.candidates.candidates = ctx_ref.candidate_items.as_mut_ptr();
    ctx_ref.candidates.content_signature = candidate_signature(&ctx_ref.candidates);
    
    if let Some(cb) = ctx_ref.candidate_callback {
        cb(ctx, &ctx_ref.candidates, ctx_ref.candidate_user_data);
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_get_candidates(ctx: *mut TypioInputContext) -> *const TypioCandidateList {
    if ctx.is_null() { return ptr::null(); }
    unsafe { &(*ctx).candidates }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_candidate_selection(ctx: *mut TypioInputContext, selected: i32) {
    if ctx.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    
    if ctx_ref.candidates.count == 0 || ctx_ref.candidates.selected == selected {
        return;
    }
    
    ctx_ref.candidates.selected = selected;
    
    if let Some(cb) = ctx_ref.candidate_callback {
        cb(ctx, &ctx_ref.candidates, ctx_ref.candidate_user_data);
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_clear_candidates(ctx: *mut TypioInputContext) {
    if ctx.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    
    ctx_ref.clear_candidates_silent();
    
    if let Some(cb) = ctx_ref.candidate_callback {
        cb(ctx, &ctx_ref.candidates, ctx_ref.candidate_user_data);
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_surrounding(
    ctx: *mut TypioInputContext, 
    text: *const c_char, 
    cursor_pos: i32, 
    anchor_pos: i32
) {
    if ctx.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    
    ctx_ref.surrounding_text = if text.is_null() {
        None
    } else {
        Some(unsafe { CStr::from_ptr(text) }.to_owned())
    };
    ctx_ref.surrounding_cursor = cursor_pos;
    ctx_ref.surrounding_anchor = anchor_pos;
}

#[no_mangle]
pub extern "C" fn typio_input_context_get_surrounding(
    ctx: *mut TypioInputContext,
    text: *mut *const c_char,
    cursor_pos: *mut i32,
    anchor_pos: *mut i32
) -> bool {
    if ctx.is_null() { return false; }
    let ctx_ref = unsafe { &*ctx };
    
    if let Some(ref s) = ctx_ref.surrounding_text {
        if !text.is_null() { unsafe { *text = s.as_ptr() }; }
        if !cursor_pos.is_null() { unsafe { *cursor_pos = ctx_ref.surrounding_cursor }; }
        if !anchor_pos.is_null() { unsafe { *anchor_pos = ctx_ref.surrounding_anchor }; }
        true
    } else {
        false
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_delete_surrounding(
    _ctx: *mut TypioInputContext,
    _offset: i32,
    _length: i32
) {
    // Placeholder
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_capabilities(ctx: *mut TypioInputContext, caps: u32) {
    if ctx.is_null() { return; }
    unsafe { (*ctx).capabilities = caps };
}

#[no_mangle]
pub extern "C" fn typio_input_context_get_capabilities(ctx: *mut TypioInputContext) -> u32 {
    if ctx.is_null() { return 0; }
    unsafe { (*ctx).capabilities }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_commit_callback(
    ctx: *mut TypioInputContext, 
    cb: Option<TypioCommitCallback>, 
    user_data: *mut c_void
) {
    if ctx.is_null() { return; }
    unsafe {
        (*ctx).commit_callback = cb;
        (*ctx).commit_user_data = user_data;
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_preedit_callback(
    ctx: *mut TypioInputContext, 
    cb: Option<TypioPreeditCallback>, 
    user_data: *mut c_void
) {
    if ctx.is_null() { return; }
    unsafe {
        (*ctx).preedit_callback = cb;
        (*ctx).preedit_user_data = user_data;
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_candidate_callback(
    ctx: *mut TypioInputContext, 
    cb: Option<TypioCandidateCallback>, 
    user_data: *mut c_void
) {
    if ctx.is_null() { return; }
    unsafe {
        (*ctx).candidate_callback = cb;
        (*ctx).candidate_user_data = user_data;
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_user_data(ctx: *mut TypioInputContext, data: *mut c_void) {
    if ctx.is_null() { return; }
    unsafe { (*ctx).user_data = data };
}

#[no_mangle]
pub extern "C" fn typio_input_context_get_user_data(ctx: *mut TypioInputContext) -> *mut c_void {
    if ctx.is_null() { return ptr::null_mut(); }
    unsafe { (*ctx).user_data }
}

#[no_mangle]
pub extern "C" fn typio_input_context_set_property(
    ctx: *mut TypioInputContext,
    key: *const c_char,
    value: *mut c_void,
    free_func: Option<extern "C" fn(*mut c_void)>
) {
    if ctx.is_null() || key.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    let key_str = unsafe { CStr::from_ptr(key) }.to_string_lossy().into_owned();
    
    if let Some(prop) = ctx_ref.properties.iter_mut().find(|p| p.key == key_str) {
        if let Some(ff) = prop.free_func {
            if !prop.value.is_null() {
                ff(prop.value);
            }
        }
        prop.value = value;
        prop.free_func = free_func;
    } else {
        ctx_ref.properties.push(PropertyEntry {
            key: key_str,
            value,
            free_func,
        });
    }
}

#[no_mangle]
pub extern "C" fn typio_input_context_get_property(
    ctx: *mut TypioInputContext,
    key: *const c_char
) -> *mut c_void {
    if ctx.is_null() || key.is_null() { return ptr::null_mut(); }
    let ctx_ref = unsafe { &mut *ctx };
    let key_str = unsafe { CStr::from_ptr(key) }.to_string_lossy();
    
    if let Some(prop) = ctx_ref.properties.iter().find(|p| p.key == key_str) {
        prop.value
    } else {
        ptr::null_mut()
    }
}
