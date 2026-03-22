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

# libhfs sources — select OS abstraction layer per platform
if host_machine.system() == 'windows'
  libhfs_os_src = files('libhfs/os/win32.c')
else
  libhfs_os_src = files('libhfs/os/unix.c')
endif

libhfs_src = libhfs_os_src + files(
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
if host_machine.system() != 'windows'
  libhfs_config.set('HAVE_UNISTD_H', 1)
endif
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

# libdmg-hfsplus (HFS+ support)
zlib_dep = dependency('zlib')

libdmg_hfsplus_inc = include_directories(
  'lib/libdmg-hfsplus/includes',
  'lib/libdmg-hfsplus/includes/hfs',
)

libdmg_hfsplus_src = files(
  'lib/libdmg-hfsplus/hfs/btree.c',
  'lib/libdmg-hfsplus/hfs/catalog.c',
  'lib/libdmg-hfsplus/hfs/extents.c',
  'lib/libdmg-hfsplus/hfs/fastunicodecompare.c',
  'lib/libdmg-hfsplus/hfs/flatfile.c',
  'lib/libdmg-hfsplus/hfs/hfslib.c',
  'lib/libdmg-hfsplus/hfs/hfscompress.c',
  'lib/libdmg-hfsplus/hfs/rawfile.c',
  'lib/libdmg-hfsplus/hfs/utility.c',
  'lib/libdmg-hfsplus/hfs/volume.c',
  'lib/libdmg-hfsplus/hfs/xattr.c',
  'lib/libdmg-hfsplus/hfs/abstractfile.c',
  'lib/libdmg-hfsplus/hfs/endian_init.c',
)

libdmg_hfsplus_c_args = []
if cc.get_id() != 'msvc'
  libdmg_hfsplus_c_args += [
    '-D_GNU_SOURCE',
    '-Wno-unused-parameter',
    '-Wno-sign-compare',
    '-Wno-address-of-packed-member',
    '-Wno-maybe-uninitialized',
    '-Wno-unused-but-set-variable',
  ]
endif

libdmg_hfsplus = static_library('dmg-hfsplus',
  libdmg_hfsplus_src,
  include_directories : libdmg_hfsplus_inc,
  dependencies : zlib_dep,
  c_args : libdmg_hfsplus_c_args,
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

binhex_c_args = ['-DHAVE_CONFIG_H']
if cc.get_id() != 'msvc'
  binhex_c_args += ['-D_GNU_SOURCE']
endif

binhex_lib = static_library('binhex',
  files('binhex.c', 'crc.c'),
  include_directories : [libhfs_inc],
  c_args : binhex_c_args,
)

# libhfsbrowse — UI-independent library for HFS/HFS+ volume operations
hfsbrowse_inc = include_directories('lib/hfsbrowse')

hfsbrowse_lib = static_library('hfsbrowse',
  files(
    'lib/hfsbrowse/encoding.cpp',
    'lib/hfsbrowse/faf.cpp',
    'lib/hfsbrowse/icon.cpp',
    'lib/hfsbrowse/appledouble.cpp',
    'lib/hfsbrowse/volume.cpp',
  ),
  include_directories : [hfsbrowse_inc, libhfs_inc, libdmg_hfsplus_inc, include_directories('gui')],
  dependencies : zlib_dep,
  link_with : [libhfs, libdmg_hfsplus],
)

gui_sources = files(
  'gui/main_gui.cpp',
  'gui/app.cpp',
  'gui/hfsplus_ops.cpp',
)

executable('hfsbrowser',
  gui_sources + imgui_sources,
  include_directories : [libhfs_inc, libdmg_hfsplus_inc, hfsbrowse_inc, imgui_inc, include_directories('gui'), include_directories('.')],
  dependencies : [sdl2_dep, gl_dep, thread_dep, zlib_dep],
  link_with : [libhfs, libdmg_hfsplus, binhex_lib, hfsbrowse_lib],
  link_args : gui_link_args,
  install : true,
)
