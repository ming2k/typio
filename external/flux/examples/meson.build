xdg_shell_xml = wayland_protocols_dir / 'stable' / 'xdg-shell' / 'xdg-shell.xml'

xdg_shell_header = custom_target('xdg-shell-client-header',
  input   : xdg_shell_xml,
  output  : 'xdg-shell-client-protocol.h',
  command : [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)
xdg_shell_code = custom_target('xdg-shell-client-code',
  input   : xdg_shell_xml,
  output  : 'xdg-shell-protocol.c',
  command : [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

executable('hello_rect',
  ['hello_rect.c', xdg_shell_header, xdg_shell_code],
  dependencies : [flux_dep] + wayland_deps,
  install      : false,
)
