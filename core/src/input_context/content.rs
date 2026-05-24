//! Composition (preedit + candidates) and surrounding text management.
//!
//! ADR-0011: composition is one transactional value emitted via
//! `set_composition`; commit is a separate ordered event. `get_preedit` /
//! `get_candidates` remain as read projections of the stored composition.

use super::{candidate_signature, TypioCandidate, TypioCandidateList, TypioComposition, TypioInputContext, TypioPreedit, TypioPreeditSegment};
use std::ffi::{c_char, CStr};
use std::ptr;

#[no_mangle]
pub extern "C" fn typio_input_context_commit(ctx: *mut TypioInputContext, text: *const c_char) {
    if ctx.is_null() || text.is_null() { return; }

    let ctx_ref = unsafe { &mut *ctx };

    // Commit clears the in-flight composition. It is silent: the commit
    // callback owns the resulting UI (clear preedit on the wire, hide popup),
    // so we do not also fire a composition update.
    ctx_ref.clear_preedit_silent();
    ctx_ref.clear_candidates_silent();
    ctx_ref.revision = ctx_ref.revision.wrapping_add(1);

    if let Some(cb) = ctx_ref.commit_callback {
        cb(ctx, text, ctx_ref.commit_user_data);
    }
}

/// Set the entire in-flight composition (preedit + candidates) atomically and
/// fire the composition callback once. An empty composition is the Idle state.
#[no_mangle]
pub extern "C" fn typio_input_context_set_composition(
    ctx: *mut TypioInputContext,
    comp: *const TypioComposition,
) {
    if ctx.is_null() || comp.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    let c = unsafe { &*comp };

    ctx_ref.clear_preedit_silent();
    ctx_ref.clear_candidates_silent();

    // Preedit segments.
    let segs = if c.segment_count > 0 && !c.segments.is_null() {
        unsafe { std::slice::from_raw_parts(c.segments, c.segment_count) }
    } else {
        &[]
    };
    for seg in segs {
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
    ctx_ref.preedit.segment_count = ctx_ref.preedit_segments.len();
    ctx_ref.preedit.cursor_pos = c.cursor_pos;
    ctx_ref.preedit.segments = ctx_ref.preedit_segments.as_mut_ptr();

    // Candidates.
    let cands = if c.candidate_count > 0 && !c.candidates.is_null() {
        unsafe { std::slice::from_raw_parts(c.candidates, c.candidate_count) }
    } else {
        &[]
    };
    for cand in cands {
        ctx_ref.candidate_items.push(TypioCandidate {
            text: if cand.text.is_null() { ptr::null() } else { unsafe { CStr::from_ptr(cand.text) }.to_owned().into_raw() as *const c_char },
            comment: if cand.comment.is_null() { ptr::null() } else { unsafe { CStr::from_ptr(cand.comment) }.to_owned().into_raw() as *const c_char },
            label: if cand.label.is_null() { ptr::null() } else { unsafe { CStr::from_ptr(cand.label) }.to_owned().into_raw() as *const c_char },
        });
    }
    ctx_ref.candidates.count = ctx_ref.candidate_items.len();
    ctx_ref.candidates.page = c.page;
    ctx_ref.candidates.page_size = c.page_size;
    ctx_ref.candidates.total = c.total;
    ctx_ref.candidates.selected = c.selected;
    ctx_ref.candidates.has_prev = c.has_prev;
    ctx_ref.candidates.has_next = c.has_next;
    ctx_ref.candidates.candidates = ctx_ref.candidate_items.as_mut_ptr();
    ctx_ref.candidates.content_signature = candidate_signature(&ctx_ref.candidates);

    ctx_ref.emit_composition(ctx);
}

/// Clear the composition to the Idle state (empty preedit + candidates) and
/// fire the composition callback.
#[no_mangle]
pub extern "C" fn typio_input_context_clear(ctx: *mut TypioInputContext) {
    if ctx.is_null() { return; }
    let ctx_ref = unsafe { &mut *ctx };
    ctx_ref.clear_preedit_silent();
    ctx_ref.clear_candidates_silent();
    ctx_ref.emit_composition(ctx);
}

#[no_mangle]
pub extern "C" fn typio_input_context_get_preedit(ctx: *mut TypioInputContext) -> *const TypioPreedit {
    if ctx.is_null() { return ptr::null(); }
    unsafe { &(*ctx).preedit }
}

#[no_mangle]
pub extern "C" fn typio_input_context_get_candidates(ctx: *mut TypioInputContext) -> *const TypioCandidateList {
    if ctx.is_null() { return ptr::null(); }
    unsafe { &(*ctx).candidates }
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
