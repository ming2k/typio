#include "typio/engine_label.h"

#include <string.h>

const char *typio_engine_label_fallback(const char *engine_name) {
    if (!engine_name || !*engine_name) {
        return "";
    }

    if (strcmp(engine_name, "basic") == 0) {
        return "Basic";
    }
    if (strcmp(engine_name, "rime") == 0) {
        return "Rime";
    }
    if (strcmp(engine_name, "mozc") == 0) {
        return "Mozc";
    }
    if (strcmp(engine_name, "whisper") == 0) {
        return "Whisper";
    }
    if (strcmp(engine_name, "sherpa-onnx") == 0) {
        return "Sherpa ONNX";
    }

    return engine_name;
}

const char *typio_engine_label_from_info(const TypioEngineInfo *info) {
    if (info && info->display_name && *info->display_name) {
        return info->display_name;
    }

    return typio_engine_label_fallback(info ? info->name : NULL);
}
