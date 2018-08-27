import os
import platform

OS_NAME=platform.system()
env=DefaultEnvironment().Clone()
LIB_DIR=os.environ['LIB_DIR'];
BIN_DIR=os.environ['BIN_DIR'];


LIB_SOURCES=Glob('src/c/*.cpp') + Glob('src/core/*.cpp') \
 + Glob('src/gpu/*.cpp')  \
 + Glob('src/opts/*sse*.cpp')  \
 + Glob('src/opts/*SSE*.cpp')  \
 + Glob('src/gpu/gl/*.cpp')  \
 + Glob('src/gpu/mock/*.cpp')  \
 + Glob('src/gpu/gl/builders/*.cpp')  \
 + Glob('src/gpu/glsl/*.cpp')  \
 + Glob('src/gpu/effects/*.cpp')  \
 + Glob('src/gpu/effects/*/*.cpp')  \
 + Glob('src/gpu/text/*.cpp')  \
 + Glob('src/gpu/ops/*.cpp')  \
 + Glob('src/gpu/mtl/*.cpp')  \
 + Glob('src/gpu/ccpr/*.cpp')  \
 + Glob('src/image/*.cpp')  \
 + Glob('src/sksl/*.cpp')  \
 + Glob('src/sksl/*/*.cpp')  \
 + Glob('src/pathops/*.cpp')  \
 + Glob('src/shaders/*.cpp')  \
 + Glob('src/shaders/*/*.cpp')  \
 + Glob('src/pipe/*.cpp')  \
 + Glob('src/utils/*.cpp')  \
 + Glob('src/utils/mac/*.cpp')  \
 + Glob('src/effects/*.cpp')  \
 + Glob('src/effects/*/*.cpp')  \
 + Glob('src/effects/imagefilters/*.cpp')  \
 + Glob('src/jumper/*.cpp')  \
 + Glob('src/sfnt/*.cpp') \
 + Glob('third_party/skcms/*.cc') \
 + [
  'src/ports/SkGlobalInitialization_default.cpp',
  'src/ports/SkGlobalInitialization_default_imagefilters.cpp',
  'src/opts/opts_check_x86.cpp', 
  'src/opts/SkOpts_avx.cpp',
  'src/opts/SkOpts_hsw.cpp'
  ]

LIB_SOURCES.remove(Glob('src/gpu/gl/GrGLMakeNativeInterface_none.cpp')[0]);

if OS_NAME == 'Darwin':
  PLATFORM_SOURCES = [
    'src/codec/SkMasks.cpp',
    'src/codec/SkBmpCodec.cpp',
    'src/ports/SkFontHost_mac.cpp',
    'src/ports/SkMemory_malloc.cpp', 
    'src/ports/SkOSFile_posix.cpp', 
    'src/ports/SkImageGenerator_none.cpp',
    'src/ports/SkOSFile_stdio.cpp', 
    'src/ports/SkDebug_stdio.cpp', 
    'src/ports/SkTLS_pthread.cpp',
    'src/gpu/gl/mac/GrGLMakeNativeInterface_mac.cpp'
  ]
  env['CCFLAGS'] = env['CCFLAGS'] + ' -std=c++14 '
elif OS_NAME == 'Linux':
  PLATFORM_SOURCES = [
    'src/codec/SkMasks.cpp',
    'src/codec/SkBmpCodec.cpp',
    'src/ports/SkMemory_malloc.cpp', 
    'src/ports/SkOSFile_posix.cpp', 
    'src/ports/SkImageGenerator_none.cpp',
    'src/ports/SkOSFile_stdio.cpp', 
    'src/ports/SkDebug_stdio.cpp',
    'src/ports/SkFontMgr_fontconfig.cpp',
    'src/ports/SkFontMgr_fontconfig_factory.cpp',
    'src/ports/SkTLS_pthread.cpp',
    'src/gpu/gl/glx/GrGLMakeNativeInterface_glx.cpp'
  ] + Glob('src/ports/SkFontHost_FreeType*.cpp');
  env['CCFLAGS'] = env['CCFLAGS'] + ' -std=c++14 '
  LIB_SOURCES.remove(Glob('src/opts/SkBitmapProcState_opts_SSSE3.cpp')[0]);

elif OS_NAME == 'Windows':
  PLATFORM_SOURCES = [
    'src/codec/SkMasks.cpp',
    'src/codec/SkBmpCodec.cpp',
    'src/ports/SkMemory_malloc.cpp', 
    'src/ports/SkOSFile_win.cpp', 
    'src/ports/SkOSLibrary_win.cpp', 
    'src/ports/SkDebug_win.cpp', 
    'src/ports/SkTLS_win.cpp',
	'src/ports/SkFontHost_win.cpp',
	'src/ports/SkDebug_win.cpp',
	'src/ports/SkRemotableFontMgr_win_dw.cpp',
    'src/ports/SkTypeface_win_dw.cpp'
  ]

LIB_SOURCES.remove(Glob('src/utils/SkLua.cpp')[0]);
LIB_SOURCES.remove(Glob('src/utils/SkLuaCanvas.cpp')[0]);
env.Library(os.path.join(LIB_DIR, 'skia'), LIB_SOURCES + PLATFORM_SOURCES)

env['LIBS'] = ['skia'] + env['LIBS']

BIN_SOURCES=Glob('example/SkiaSDLExample.cpp')
env.Program(os.path.join(BIN_DIR, 'demo_skia'), BIN_SOURCES)
