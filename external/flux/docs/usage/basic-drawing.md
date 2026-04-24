# Basic Drawing

This page shows how to express common primitive drawing tasks with flux.

## Draw a Line

flux draws lines as stroked paths:

```c
fx_canvas *c = fx_surface_acquire(surface);

fx_path *line = fx_path_create();
fx_path_move_to(line, 24.0f, 32.0f);
fx_path_line_to(line, 240.0f, 96.0f);

fx_paint stroke;
fx_paint_init(&stroke, fx_color_rgba(255, 230, 120, 255));
stroke.stroke_width = 3.0f;
stroke.line_cap = FX_CAP_ROUND;
stroke.line_join = FX_JOIN_ROUND;

fx_stroke_path(c, line, &stroke);

fx_surface_present(surface);
fx_path_destroy(line);
```

If the path is reused every frame, create it once, keep it alive while frames
are recorded, and destroy it when the scene object is destroyed.

## Draw a Circle

Use SVG-style arc commands or cubic curves to build a circle. This example uses
two 180-degree arcs:

```c
fx_canvas *c = fx_surface_acquire(surface);

fx_path *circle = fx_path_create();
float cx = 160.0f;
float cy = 120.0f;
float r = 48.0f;

fx_path_move_to(circle, cx + r, cy);
fx_path_arc_to(circle, r, r, 0.0f, false, true, cx - r, cy);
fx_path_arc_to(circle, r, r, 0.0f, false, true, cx + r, cy);
fx_path_close(circle);

fx_paint fill;
fx_paint_init(&fill, fx_color_rgba(90, 180, 255, 255));
fx_fill_path(c, circle, &fill);

fx_paint outline;
fx_paint_init(&outline, fx_color_rgba(240, 250, 255, 255));
outline.stroke_width = 2.0f;
fx_stroke_path(c, circle, &outline);

fx_surface_present(surface);
fx_path_destroy(circle);
```

## Fill a Rectangle

For axis-aligned rectangles, use the convenience helper:

```c
fx_canvas *c = fx_surface_acquire(surface);

fx_fill_rect(c, &(fx_rect){ 20.0f, 20.0f, 160.0f, 80.0f },
             fx_color_rgba(40, 120, 220, 255));

fx_surface_present(surface);
```

## Transform Drawing

Transforms are part of canvas recording:

```c
fx_canvas *c = fx_surface_acquire(surface);

fx_save(c);
fx_translate(c, 320.0f, 180.0f);
fx_rotate(c, 0.25f);
fx_scale(c, 2.0f, 2.0f);

fx_fill_rect(c, &(fx_rect){ -25.0f, -25.0f, 50.0f, 50.0f },
             fx_color_rgba(255, 120, 80, 255));

fx_restore(c);
fx_surface_present(surface);
```

## Gradient Fill

Attach a gradient to `fx_paint` and use the same path drawing API:

```c
fx_canvas *c = fx_surface_acquire(surface);

fx_path *rect = fx_path_create();
fx_path_add_rect(rect, &(fx_rect){ 40.0f, 40.0f, 240.0f, 120.0f });

fx_gradient *gradient = fx_gradient_create_linear(ctx, &(fx_linear_gradient_desc){
    .start = { 40.0f, 40.0f },
    .end = { 280.0f, 40.0f },
    .colors = {
        fx_color_rgba(255, 120, 60, 255),
        fx_color_rgba(60, 160, 255, 255),
    },
    .stops = { 0.0f, 1.0f },
    .stop_count = 2,
});

fx_paint paint;
fx_paint_init(&paint, fx_color_rgba(255, 255, 255, 255));
fx_paint_set_gradient(&paint, gradient);
fx_fill_path(c, rect, &paint);

fx_surface_present(surface);
fx_gradient_destroy(gradient);
fx_path_destroy(rect);
```
