# Text

flux consumes positioned glyph runs. It does not discover system fonts or shape
UTF-8 text into glyph IDs. Keep font discovery, fallback, paragraph layout,
bidirectional text, and line breaking above flux.

## Dependency Split

- Fontconfig: find a system font file such as `Noto Sans Regular`.
- HarfBuzz: shape UTF-8 text into glyph IDs and positioned advances.
- flux: load the font file, populate an `fx_glyph_run`, atlas glyphs, and render.

## Direct Glyph Run

Use this only when the caller already knows glyph IDs:

```c
fx_font *font = fx_font_create(ctx, &(fx_font_desc){
    .family = "Noto Sans",
    .source_name = "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    .size = 18.0f,
});

fx_glyph_run *run = fx_glyph_run_create(8);
fx_glyph_run_append(run, 43, 0.0f, 0.0f);   /* glyph id for illustration only */
fx_glyph_run_append(run, 72, 12.0f, 0.0f);

fx_canvas *c = fx_surface_acquire(surface);

fx_paint text_paint;
fx_paint_init(&text_paint, fx_color_rgba(245, 245, 240, 255));
fx_draw_glyph_run(c, font, run, 32.0f, 64.0f, &text_paint);

fx_surface_present(surface);
fx_glyph_run_destroy(run);
fx_font_destroy(font);
```

## Shape Text With HarfBuzz

The application can borrow the HarfBuzz font from flux and convert HarfBuzz
output into a flux glyph run:

```c
#include <harfbuzz/hb.h>

fx_glyph_run *shape_text_with_harfbuzz(fx_font *font, const char *utf8)
{
    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8, -1, 0, -1);
    hb_buffer_guess_segment_properties(buf);

    hb_font_t *hb_font = fx_font_get_hb_font(font);
    hb_shape(hb_font, buf, NULL, 0);

    unsigned int count = 0;
    hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buf, &count);
    hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);

    fx_glyph_run *run = fx_glyph_run_create(count);
    float x = 0.0f;
    float y = 0.0f;
    for (unsigned int i = 0; i < count; ++i) {
        fx_glyph_run_append(run, infos[i].codepoint,
                            x + pos[i].x_offset / 64.0f,
                            y - pos[i].y_offset / 64.0f);
        x += pos[i].x_advance / 64.0f;
        y -= pos[i].y_advance / 64.0f;
    }

    hb_buffer_destroy(buf);
    return run;
}
```

Render the shaped run:

```c
fx_glyph_run *run = shape_text_with_harfbuzz(font, "Hello flux");
fx_canvas *c = fx_surface_acquire(surface);

fx_paint paint;
fx_paint_init(&paint, fx_color_rgba(255, 255, 255, 255));
fx_draw_glyph_run(c, font, run, 40.0f, 80.0f, &paint);

fx_surface_present(surface);
fx_glyph_run_destroy(run);
```

## Production Text Layer

For production UI text, keep these policies outside flux:

- Font discovery and fallback through Fontconfig or the toolkit's font layer.
- Paragraph layout, wrapping, truncation, and alignment.
- Bidirectional text handling and script itemization.
- Emoji fallback and color glyph policy.
- Text caching and invalidation.

After those steps, submit final positioned glyph runs to flux.
