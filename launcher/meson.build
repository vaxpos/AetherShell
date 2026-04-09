project('launcher', 'c',
  version : '0.1.0',
  license : 'GPL-3.0-or-later',
  default_options : [
    'c_std=c11',
    'warning_level=2',
    'optimization=2',
  ]
)

# ── Dependencies ─────────────────────────────────────────────────────────────
gtk3_dep    = dependency('gtk+-3.0',  version : '>= 3.22')
gtk_layer_shell_dep = dependency('gtk-layer-shell-0', version : '>= 0.8.0')

# ── pkg data dir define ──────────────────────────────────────────────────────
pkg_datadir = join_paths(get_option('prefix'), get_option('datadir'), meson.project_name())
add_project_arguments('-DPKGDATADIR="' + pkg_datadir + '"', language : 'c')

# ── Source files ─────────────────────────────────────────────────────────────
sources = files(
  'src/main.c',

  # Core layer (no GTK)
  'src/core/app_entry.c',
  'src/core/desktop_reader.c',
  'src/core/icon_loader.c',

  # Utils
  'src/utils/string_utils.c',

  # UI layer
  'src/ui/app_icon.c',
  'src/ui/search_bar.c',
  'src/ui/app_grid.c',
  'src/ui/launcher_window.c',
)

# ── Compiler flags ───────────────────────────────────────────────────────────
c_args = [
  '-Wall',
  '-Wextra',
  '-Wno-unused-parameter',
  '-march=native',
]

# ── Executable ───────────────────────────────────────────────────────────────
executable('launcher',
  sources,
  dependencies : [gtk3_dep, gtk_layer_shell_dep],
  c_args       : c_args,
  link_args    : ['-lm', '-lpthread'],
  install      : true,
)

# ── Data ─────────────────────────────────────────────────────────────────────
subdir('data')
