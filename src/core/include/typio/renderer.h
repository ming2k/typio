#ifndef TYPIO_RENDERER_H
#define TYPIO_RENDERER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float r, g, b, a;
} TypioColor;

typedef struct TypioTextLayout TypioTextLayout;

typedef struct {
    void (*draw_rect)(void *canvas, float x, float y, float w, float h, TypioColor color);
    void (*draw_text)(void *canvas, TypioTextLayout *layout, float x, float y, TypioColor color);
    void (*clear)(void *canvas);
} TypioCanvasVTable;

typedef struct {
    TypioCanvasVTable *vtable;
    void *priv;
} TypioCanvas;

typedef struct {
    TypioTextLayout *(*create_layout)(void *engine, const char *text, const char *font_desc);
    void (*get_metrics)(TypioTextLayout *layout, float *out_w, float *out_h);
    void (*free_layout)(TypioTextLayout *layout);
} TypioTextEngineVTable;

typedef struct {
    TypioTextEngineVTable *vtable;
    void *priv;
} TypioTextEngine;

#endif
