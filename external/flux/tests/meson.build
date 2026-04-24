test_foundation = executable('test_foundation',
  'test_foundation.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_path_flatten = executable('test_path_flatten',
  'test_path_flatten.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_arc = executable('test_arc',
  'test_arc.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_tess = executable('test_tess',
  'test_tess.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_stroker = executable('test_stroker',
  'test_stroker.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_transforms = executable('test_transforms',
  'test_transforms.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_offscreen = executable('test_offscreen',
  'test_offscreen.c',
  dependencies : [flux_dep],
)

test_transform_stack = executable('test_transform_stack',
  'test_transform_stack.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_fill_rect = executable('test_fill_rect',
  'test_fill_rect.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_image_update = executable('test_image_update',
  'test_image_update.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_error_logging = executable('test_error_logging',
  'test_error_logging.c',
  dependencies : [flux_dep],
)

test_gradient = executable('test_gradient',
  'test_gradient.c',
  dependencies : [flux_dep],
)

test_missing_apis = executable('test_missing_apis',
  'test_missing_apis.c',
  dependencies : [flux_dep],
)

test_clip = executable('test_clip',
  'test_clip.c',
  dependencies : [flux_dep],
)

test('foundation', test_foundation, suite : 'unit')
test('path_flatten', test_path_flatten, suite : 'unit')
test('arc', test_arc, suite : 'unit')
test('tess', test_tess, suite : 'unit')
test('stroker', test_stroker, suite : 'unit')
test('transforms', test_transforms, suite : 'unit')
test('transform_stack', test_transform_stack, suite : 'unit')
test('fill_rect', test_fill_rect, suite : 'unit')
test('image_update', test_image_update, suite : 'unit')
test('error_logging', test_error_logging, suite : 'unit')
test('gradient', test_gradient, suite : 'integration')
test('missing_apis', test_missing_apis, suite : 'integration')
test('offscreen', test_offscreen, suite : 'integration')
test('clip', test_clip, suite : 'integration')
