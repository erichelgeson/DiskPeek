project('hfsbrowser', 'c', 'cpp',
  version : '0.1.0',
  default_options : [
    'warning_level=2',
    'buildtype=debugoptimized',
    'c_std=c11',
    'cpp_std=c++23',
  ]
)

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

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

# phosg — utility library (vendored from github.com/fuzziqersoftware/phosg)
phosg_inc = include_directories('lib')  # so <phosg/X.hh> resolves to lib/phosg/X.hh
phosg_self_inc = include_directories('lib/phosg')  # so phosg files can include each other

phosg_src = files(
  'lib/phosg/Strings.cc',
  'lib/phosg/Encoding.cc',
  'lib/phosg/Filesystem.cc',
  'lib/phosg/Filesystem-Unix.cc',
  'lib/phosg/Hash.cc',
  'lib/phosg/Time.cc',
)

phosg_cpp_args = []
if cxx.get_id() != 'msvc'
  phosg_cpp_args += [
    '-Wno-unused-parameter',
    '-Wno-sign-compare',
  ]
endif

phosg_lib = static_library('phosg',
  phosg_src,
  include_directories : [phosg_self_inc],
  dependencies : zlib_dep,
  cpp_args : phosg_cpp_args,
)

# libresource_file — Mac resource fork parser/decoder
# (vendored from github.com/fuzziqersoftware/resource_dasm)
resource_dasm_inc = include_directories('lib/resource_dasm')

resource_dasm_src = files(
  'lib/resource_dasm/ResourceFile.cc',
  'lib/resource_dasm/ResourceCompression.cc',
  'lib/resource_dasm/QuickDrawEngine.cc',
  'lib/resource_dasm/QuickDrawFormats.cc',
  'lib/resource_dasm/AudioCodecs.cc',
  'lib/resource_dasm/BitmapFontRenderer.cc',
  'lib/resource_dasm/Lookups.cc',
  'lib/resource_dasm/TextCodecs.cc',
  'lib/resource_dasm/ResourceIDs.cc',
  'lib/resource_dasm/SystemDecompressors.cc',
  'lib/resource_dasm/LowMemoryGlobals.cc',
  'lib/resource_dasm/IndexFormats/ResourceFork.cc',
  'lib/resource_dasm/ResourceDecompressors/System01.cc',
  'lib/resource_dasm/ResourceDecompressors/System2.cc',
  'lib/resource_dasm/ResourceDecompressors/System3.cc',
  'lib/resource_dasm/DataCodecs/PackBits.cc',
  'lib/resource_dasm/DataCodecs/Bungie.cc',
  'lib/resource_dasm/DataCodecs/DinoParkTycoon-LZSS-RLE.cc',
  'lib/resource_dasm/DataCodecs/MacSki-RUN4-COOK-CO2K.cc',
  'lib/resource_dasm/DataCodecs/Presage-LZSS.cc',
  'lib/resource_dasm/DataCodecs/SoundMusicSys-LZSS.cc',
  'lib/resource_dasm/Audio/Constants.cc',
  'lib/resource_dasm/Audio/Instrument.cc',
  'lib/resource_dasm/Audio/WAVFile.cc',
)

resource_dasm_cpp_args = []
if cxx.get_id() != 'msvc'
  resource_dasm_cpp_args += [
    '-Wno-unused-parameter',
    '-Wno-sign-compare',
    '-Wno-address-of-packed-member',
  ]
endif

resource_file_lib = static_library('resource_file',
  resource_dasm_src,
  include_directories : [resource_dasm_inc, phosg_inc],
  dependencies : zlib_dep,
  link_with : [phosg_lib],
  cpp_args : resource_dasm_cpp_args,
)

# GUI application
sdl3_dep = dependency('sdl3')
gl_dep = dependency('gl')
thread_dep = dependency('threads')

gui_link_args = []
if host_machine.system() == 'darwin'
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
  'lib/imgui/backends/imgui_impl_sdl3.cpp',
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
    'lib/hfsbrowse/hfsbrowse.cpp',
  ),
  include_directories : [hfsbrowse_inc, libhfs_inc, libdmg_hfsplus_inc, include_directories('gui')],
  dependencies : zlib_dep,
  link_with : [libhfs, libdmg_hfsplus],
)

gui_sources = files(
  'gui/main_gui.cpp',
  'gui/app.cpp',
  'gui/hfsplus_ops.cpp',
  'gui/resedit.cpp',
)

executable('hfsbrowser',
  gui_sources + imgui_sources,
  include_directories : [libhfs_inc, libdmg_hfsplus_inc, hfsbrowse_inc, imgui_inc, resource_dasm_inc, phosg_inc, include_directories('gui'), include_directories('.')],
  dependencies : [sdl3_dep, gl_dep, thread_dep, zlib_dep],
  link_with : [libhfs, libdmg_hfsplus, binhex_lib, hfsbrowse_lib, resource_file_lib, phosg_lib],
  link_args : gui_link_args,
  install : true,
)
