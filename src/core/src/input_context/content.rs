//! Preedit, candidate, and surrounding text management

use super::{candidate_signature, TypioCandidate, TypioCandidateList, TypioInputContext, TypioPreedit, TypioPreeditSegment};
use std::ffi::{c_char, CStr};
use std::ptr;

#[no_mangle]
pub extern "C" fn typio_input_context_commit(ctx: *mut TypioInputContext, text: *const c_char) {
    if ctx.is_null() || text.is_null() { return; }

    super::typio_input_context_clear_preedit(ctx);
    super::typio_input_context_clear_candidates(ctx);

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
