//! Input context — Rust implementation of input_context.c
//!
//! Manages per-client input state: preedit, candidates, focus,
//! surrounding text, and property storage.

mod callbacks;
mod content;
mod focus;

pub use callbacks::*;
pub use content::*;
pub use focus::*;

use crate::TypioInstance;
use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;

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

/// Atomic composition snapshot: preedit + candidates as one value (ADR-0011).
/// An empty composition (no segments, no candidates) is the Idle state.
/// All pointers are borrowed and valid only for the call/callback duration.
#[repr(C)]
pub struct TypioComposition {
    pub struct_size: usize,
    /* preedit */
    pub segments: *const TypioPreeditSegment,
    pub segment_count: usize,
    pub cursor_pos: i32,
    /* candidates */
    pub candidates: *const TypioCandidate,
    pub candidate_count: usize,
    pub page: i32,
    pub page_size: i32,
    pub total: i32,
    pub selected: i32,
    pub has_prev: bool,
    pub has_next: bool,
    pub content_signature: u64,
    pub revision: u64,
}

/* Callback types */
pub type TypioCommitCallback = extern "C" fn(*mut TypioInputContext, *const c_char, *mut c_void);
pub type TypioCompositionCallback =
    extern "C" fn(*mut TypioInputContext, *const TypioComposition, *mut c_void);

pub(crate) struct PropertyEntry {
    key: String,
    value: *mut c_void,
    free_func: Option<extern "C" fn(*mut c_void)>,
}

#[repr(C)]
pub struct TypioInputContext {
    pub(crate) instance: *mut TypioInstance,
    pub(crate) focused: bool,
    pub(crate) capabilities: u32,

    pub(crate) preedit: TypioPreedit,
    pub(crate) preedit_segments: Vec<TypioPreeditSegment>,

    pub(crate) candidates: TypioCandidateList,
    pub(crate) candidate_items: Vec<TypioCandidate>,

    pub(crate) surrounding_text: Option<CString>,
    pub(crate) surrounding_cursor: i32,
    pub(crate) surrounding_anchor: i32,

    pub(crate) commit_callback: Option<TypioCommitCallback>,
    pub(crate) commit_user_data: *mut c_void,
    pub(crate) composition_callback: Option<TypioCompositionCallback>,
    pub(crate) composition_user_data: *mut c_void,
    pub(crate) revision: u64,

    pub(crate) user_data: *mut c_void,
    pub(crate) properties: Vec<PropertyEntry>,
}

impl TypioInputContext {
    pub(crate) fn new(instance: *mut TypioInstance) -> Self {
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
            composition_callback: None,
            composition_user_data: ptr::null_mut(),
            revision: 0,

            user_data: ptr::null_mut(),
            properties: Vec::new(),
        }
    }

    /// Bump the revision and fire the composition callback with the current
    /// preedit + candidate state as one atomic snapshot.
    pub(crate) fn emit_composition(&mut self, ctx: *mut TypioInputContext) {
        self.revision = self.revision.wrapping_add(1);
        let cb = match self.composition_callback {
            Some(cb) => cb,
            None => return,
        };
        let ud = self.composition_user_data;
        let comp = TypioComposition {
            struct_size: std::mem::size_of::<TypioComposition>(),
            segments: self.preedit.segments as *const TypioPreeditSegment,
            segment_count: self.preedit.segment_count,
            cursor_pos: self.preedit.cursor_pos,
            candidates: self.candidates.candidates as *const TypioCandidate,
            candidate_count: self.candidates.count,
            page: self.candidates.page,
            page_size: self.candidates.page_size,
            total: self.candidates.total,
            selected: self.candidates.selected,
            has_prev: self.candidates.has_prev,
            has_next: self.candidates.has_next,
            content_signature: self.candidates.content_signature,
            revision: self.revision,
        };
        cb(ctx, &comp, ud);
    }

    pub(crate) fn clear_preedit_silent(&mut self) {
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

    pub(crate) fn clear_candidates_silent(&mut self) {
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

pub(super) fn signature_bytes(mut hash: u64, bytes: &[u8]) -> u64 {
    for &b in bytes {
        hash ^= b as u64;
        hash = hash.wrapping_mul(TYPIO_CANDIDATE_SIGNATURE_PRIME);
    }
    hash
}

pub(super) fn signature_string(mut hash: u64, text: *const c_char) -> u64 {
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

pub(super) fn candidate_signature(list: &TypioCandidateList) -> u64 {
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
