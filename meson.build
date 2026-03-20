project('hfsbrowser', 'c', 'cpp',
  version : '0.1.0',
  default_options : [
    'warning_level=2',
    'buildtype=debugoptimized',
    'c_std=c11',
    'cpp_std=c++17',
  ]
)

cc = meson.get_compiler('c')

# libhfs sources
libhfs_src = files(
  'libhfs/os/unix.c',
  'libhfs/data.c',
  'libhfs/block.c',
  'libhfs/low.c',
  'libhfs/medium.c',
  'libhfs/file.c',
  'libhfs/btree.c',
  'libhfs/node.c',
  'libhfs/record.c',
  'libhfs/volume.c',
  'libhfs/hfs.c',
  'libhfs/version.c',
)

libhfs_inc = include_directories('libhfs')

# Generate config.h for libhfs (replaces autoconf)
libhfs_config = configuration_data()
libhfs_config.set('HAVE_FCNTL_H', 1)
libhfs_config.set('HAVE_UNISTD_H', 1)
libhfs_config.set('HAVE_MKTIME', 1)
libhfs_config.set('HAVE_STDINT_H', 1)
libhfs_config.set('HAVE_INTTYPES_H', 1)
libhfs_config.set('HAVE_STDIO_H', 1)
libhfs_config.set('HAVE_STDLIB_H', 1)
libhfs_config.set('HAVE_STRING_H', 1)
libhfs_config.set('HAVE_STRINGS_H', 1)
libhfs_config.set('HAVE_SYS_STAT_H', 1)
libhfs_config.set('HAVE_SYS_TYPES_H', 1)
libhfs_config.set('STDC_HEADERS', 1)
configure_file(
  output : 'config.h',
  configuration : libhfs_config,
)

libhfs = static_library('hfs',
  libhfs_src,
  include_directories : libhfs_inc,
  c_args : ['-DHAVE_CONFIG_H'],
)

# GUI application
sdl2_dep = dependency('sdl2')
gl_dep = dependency('gl')
thread_dep = dependency('threads')

gui_link_args = []
if host_machine.system() == 'darwin'
  sdl2_dep = dependency('sdl2', static : true)
  gui_link_args += [
    '-framework', 'CoreAudio',
    '-framework', 'AudioToolbox',
    '-framework', 'CoreHaptics',
    '-framework', 'GameController',
    '-framework', 'ForceFeedback',
    '-framework', 'CoreVideo',
    '-framework', 'Cocoa',
    '-framework', 'Carbon',
    '-framework', 'IOKit',
    '-framework', 'Metal',
    '-framework', 'QuartzCore',
  ]
endif

imgui_inc = include_directories('lib/imgui', 'lib/imgui/backends')

imgui_sources = files(
  'lib/imgui/imgui.cpp',
  'lib/imgui/imgui_draw.cpp',
  'lib/imgui/imgui_tables.cpp',
  'lib/imgui/imgui_widgets.cpp',
  'lib/imgui/backends/imgui_impl_sdl2.cpp',
  'lib/imgui/backends/imgui_impl_opengl3.cpp',
)

gui_sources = files(
  'gui/main_gui.cpp',
  'gui/app.cpp',
)

executable('hfsbrowser',
  gui_sources + imgui_sources,
  include_directories : [libhfs_inc, imgui_inc, include_directories('gui')],
  dependencies : [sdl2_dep, gl_dep, thread_dep],
  link_with : libhfs,
  link_args : gui_link_args,
  install : true,
)
