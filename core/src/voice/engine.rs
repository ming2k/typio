//! Voice engine adapters — replaces `voice_engine_sherpa.c` and `voice_engine_whisper.c`.
//!
//! `SherpaEngine` and `WhisperEngine` are pure-Rust implementations of the
//! `Engine` and `VoiceEngine` traits.  The only remaining `extern "C"` symbols
//! are thin factory wrappers needed until the engine-manager registration is
//! trait-ified in Phase 3.

// Every item below is used only by the feature-gated `sherpa`/`whisper`
// submodules and factory wrappers, so the imports are gated to match.
#[cfg(any(feature = "have_sherpa_onnx", feature = "have_whisper"))]
use crate::engine::r#trait::{Engine, EngineInfo, VoiceEngine};
#[cfg(any(feature = "have_sherpa_onnx", feature = "have_whisper"))]
use crate::engine::rust_adapter::create_wrapper_typio_engine;
#[cfg(any(feature = "have_sherpa_onnx", feature = "have_whisper"))]
use crate::types::*;
#[cfg(any(feature = "have_sherpa_onnx", feature = "have_whisper"))]
use crate::voice::proxy::{CBackend, VoiceBackendProxy};
#[cfg(any(feature = "have_sherpa_onnx", feature = "have_whisper"))]
use crate::{TypioInstance, TypioInputContext};
#[cfg(any(feature = "have_sherpa_onnx", feature = "have_whisper"))]
use std::ffi::{CStr, CString};
#[cfg(any(feature = "have_sherpa_onnx", feature = "have_whisper"))]
use std::sync::Arc;

#[cfg(feature = "have_sherpa_onnx")]
mod sherpa {
    use super::*;
    use crate::voice::backend::typio_voice_backend_sherpa_new;
    use std::sync::atomic::AtomicPtr;

    pub static INFO: EngineInfo = EngineInfo {
        name: c"sherpa-onnx".as_ptr(),
        display_name: c"Sherpa-ONNX".as_ptr(),
        description: c"Speech-to-text via sherpa-onnx".as_ptr(),
        version: c"1.0".as_ptr(),
        author: c"Typio".as_ptr(),
        icon: std::ptr::null(),
        language: std::ptr::null(),
        type_: TypioEngineType::TypioEngineTypeVoice,
        capabilities: TypioEngineCapability::TypioCapVoiceInput as u32,
    };

    pub static C_INFO: TypioEngineInfo = TypioEngineInfo {
        name: c"sherpa-onnx".as_ptr(),
        display_name: c"Sherpa-ONNX".as_ptr(),
        description: c"Speech-to-text via sherpa-onnx".as_ptr(),
        version: c"1.0".as_ptr(),
        author: c"Typio".as_ptr(),
        icon: std::ptr::null(),
        language: std::ptr::null(),
        type_: TypioEngineType::TypioEngineTypeVoice,
        capabilities: TypioEngineCapability::TypioCapVoiceInput as u32,
        api_version: 1,
        struct_size: std::mem::size_of::<TypioEngineInfo>(),
    };

    pub struct SherpaEngine {
        proxy: Arc<VoiceBackendProxy>,
        instance: AtomicPtr<TypioInstance>,
    }

    impl SherpaEngine {
        pub fn new() -> Self {
            Self {
                proxy: Arc::new(VoiceBackendProxy::new()),
                instance: AtomicPtr::new(std::ptr::null_mut()),
            }
        }
    }

    impl Engine for SherpaEngine {
        fn info(&self) -> &EngineInfo {
            &INFO
        }

        fn init(&mut self, instance: &mut TypioInstance) -> TypioResult {
            self.instance.store(instance, std::sync::atomic::Ordering::SeqCst);
            self.proxy = Arc::new(VoiceBackendProxy::new());
            TypioResult::TypioOk
        }

        fn deactivate(&mut self) {
            self.proxy.clear_impl();
            crate::voice::log_msg(
                TypioLogLevel::TypioLogInfo,
                "Sherpa-ONNX: model freed on deactivate",
            );
        }

        fn focus_in(&mut self, _ctx: *mut TypioInputContext) {
            if self.proxy.is_ready() {
                return;
            }

            let instance = self.instance.load(std::sync::atomic::Ordering::SeqCst);

            let data_dir = crate::instance::typio_instance_get_data_dir(instance);
            let data_dir = if data_dir.is_null() {
                CString::new("").unwrap()
            } else {
                unsafe { CStr::from_ptr(data_dir).to_owned() }
            };

            let mut language: Option<CString> = None;
            let mut model: Option<CString> = None;
            let ecfg = crate::instance::typio_instance_get_engine_config(instance, c"sherpa-onnx".as_ptr());
            if !ecfg.is_null() {
                unsafe {
                    let l = crate::config::typio_config_get_string(
                        ecfg,
                        c"language".as_ptr(),
                        std::ptr::null(),
                    );
                    if !l.is_null() {
                        language = Some(CStr::from_ptr(l).to_owned());
                    }
                    let m = crate::config::typio_config_get_string(
                        ecfg,
                        c"model".as_ptr(),
                        std::ptr::null(),
                    );
                    if !m.is_null() {
                        model = Some(CStr::from_ptr(m).to_owned());
                    }
                }
            }

            // Load synchronously: when focus_in returns the model is ready, so
            // the caller (voice session start) begins recording immediately.
            // This matches the original C behaviour; the previous async load
            // left the session stuck in the "loading" state.
            let lang_ptr = language.as_ref().map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
            let model_ptr = model.as_ref().map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
            let backend = unsafe {
                typio_voice_backend_sherpa_new(data_dir.as_ptr(), lang_ptr, model_ptr)
            };
            let backend = if backend.is_null() {
                None
            } else {
                unsafe { CBackend::new(backend) }.map(Arc::new)
            };
            self.proxy.set_impl(backend);

            if self.proxy.is_ready() {
                crate::voice::log_msg(TypioLogLevel::TypioLogInfo, "Sherpa-ONNX: model loaded");
            } else {
                crate::voice::log_msg(
                    TypioLogLevel::TypioLogWarning,
                    "Sherpa-ONNX: failed to load model",
                );
            }
        }

        fn focus_out(&mut self, _ctx: *mut TypioInputContext) {}
        fn reset(&mut self, _ctx: *mut TypioInputContext) {}

        fn reload_config(&mut self) -> TypioResult {
            TypioResult::TypioOk
        }

        fn as_voice(&mut self) -> Option<&mut dyn VoiceEngine> {
            Some(self)
        }
    }

    impl VoiceEngine for SherpaEngine {
        fn is_ready(&self) -> bool {
            self.proxy.is_ready()
        }

        fn process_audio(&self, samples: &[f32]) -> Option<String> {
            self.proxy.process(samples)
        }
    }
}

#[cfg(feature = "have_whisper")]
mod whisper {
    use super::*;
    use crate::voice::backend::typio_voice_backend_whisper_new;
    use std::sync::atomic::AtomicPtr;

    pub static INFO: EngineInfo = EngineInfo {
        name: c"whisper".as_ptr(),
        display_name: c"Whisper".as_ptr(),
        description: c"Speech-to-text via whisper.cpp".as_ptr(),
        version: c"1.0".as_ptr(),
        author: c"Typio".as_ptr(),
        icon: std::ptr::null(),
        language: std::ptr::null(),
        type_: TypioEngineType::TypioEngineTypeVoice,
        capabilities: TypioEngineCapability::TypioCapVoiceInput as u32,
    };

    pub static C_INFO: TypioEngineInfo = TypioEngineInfo {
        name: c"whisper".as_ptr(),
        display_name: c"Whisper".as_ptr(),
        description: c"Speech-to-text via whisper.cpp".as_ptr(),
        version: c"1.0".as_ptr(),
        author: c"Typio".as_ptr(),
        icon: std::ptr::null(),
        language: std::ptr::null(),
        type_: TypioEngineType::TypioEngineTypeVoice,
        capabilities: TypioEngineCapability::TypioCapVoiceInput as u32,
        api_version: 1,
        struct_size: std::mem::size_of::<TypioEngineInfo>(),
    };

    pub struct WhisperEngine {
        proxy: Arc<VoiceBackendProxy>,
        instance: AtomicPtr<TypioInstance>,
    }

    impl WhisperEngine {
        pub fn new() -> Self {
            Self {
                proxy: Arc::new(VoiceBackendProxy::new()),
                instance: AtomicPtr::new(std::ptr::null_mut()),
            }
        }
    }

    impl Engine for WhisperEngine {
        fn info(&self) -> &EngineInfo {
            &INFO
        }

        fn init(&mut self, instance: &mut TypioInstance) -> TypioResult {
            self.instance.store(instance, std::sync::atomic::Ordering::SeqCst);
            self.proxy = Arc::new(VoiceBackendProxy::new());
            TypioResult::TypioOk
        }

        fn deactivate(&mut self) {
            self.proxy.clear_impl();
            crate::voice::log_msg(
                TypioLogLevel::TypioLogInfo,
                "Whisper: model freed on deactivate",
            );
        }

        fn focus_in(&mut self, _ctx: *mut TypioInputContext) {
            if self.proxy.is_ready() {
                return;
            }

            let instance = self.instance.load(std::sync::atomic::Ordering::SeqCst);

            let data_dir = crate::instance::typio_instance_get_data_dir(instance);
            let data_dir = if data_dir.is_null() {
                CString::new("").unwrap()
            } else {
                unsafe { CStr::from_ptr(data_dir).to_owned() }
            };

            let mut language: Option<CString> = None;
            let mut model = CString::new("base").unwrap();
            let ecfg = crate::instance::typio_instance_get_engine_config(instance, c"whisper".as_ptr());
            if !ecfg.is_null() {
                unsafe {
                    let l = crate::config::typio_config_get_string(
                        ecfg,
                        c"language".as_ptr(),
                        std::ptr::null(),
                    );
                    if !l.is_null() {
                        language = Some(CStr::from_ptr(l).to_owned());
                    }
                    let m = crate::config::typio_config_get_string(
                        ecfg,
                        c"model".as_ptr(),
                        std::ptr::null(),
                    );
                    if !m.is_null() {
                        model = CStr::from_ptr(m).to_owned();
                    }
                }
            }

            // Load synchronously (see SherpaEngine::focus_in).
            let lang_ptr = language.as_ref().map(|s| s.as_ptr()).unwrap_or(std::ptr::null());
            let backend = unsafe {
                typio_voice_backend_whisper_new(data_dir.as_ptr(), lang_ptr, model.as_ptr())
            };
            let backend = if backend.is_null() {
                None
            } else {
                unsafe { CBackend::new(backend) }.map(Arc::new)
            };
            self.proxy.set_impl(backend);

            if self.proxy.is_ready() {
                crate::voice::log_msg(TypioLogLevel::TypioLogInfo, "Whisper: model loaded");
            } else {
                crate::voice::log_msg(
                    TypioLogLevel::TypioLogWarning,
                    "Whisper: failed to load model",
                );
            }
        }

        fn focus_out(&mut self, _ctx: *mut TypioInputContext) {}
        fn reset(&mut self, _ctx: *mut TypioInputContext) {}

        fn reload_config(&mut self) -> TypioResult {
            TypioResult::TypioOk
        }

        fn as_voice(&mut self) -> Option<&mut dyn VoiceEngine> {
            Some(self)
        }
    }

    impl VoiceEngine for WhisperEngine {
        fn is_ready(&self) -> bool {
            self.proxy.is_ready()
        }

        fn process_audio(&self, samples: &[f32]) -> Option<String> {
            self.proxy.process(samples)
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Thin C ABI factory wrappers — kept until engine-manager registration is    */
/* trait-ified (Phase 3).                                                     */
/* -------------------------------------------------------------------------- */

#[cfg(feature = "have_sherpa_onnx")]
#[no_mangle]
pub extern "C" fn typio_engine_create_sherpa() -> *mut TypioEngine {
    create_wrapper_typio_engine(Box::new(sherpa::SherpaEngine::new()), &sherpa::C_INFO)
}

#[cfg(feature = "have_sherpa_onnx")]
#[no_mangle]
pub extern "C" fn typio_engine_get_info_sherpa() -> *const TypioEngineInfo {
    &sherpa::C_INFO
}

#[cfg(feature = "have_whisper")]
#[no_mangle]
pub extern "C" fn typio_engine_create_whisper() -> *mut TypioEngine {
    create_wrapper_typio_engine(Box::new(whisper::WhisperEngine::new()), &whisper::C_INFO)
}

#[cfg(feature = "have_whisper")]
#[no_mangle]
pub extern "C" fn typio_engine_get_info_whisper() -> *const TypioEngineInfo {
    &whisper::C_INFO
}
