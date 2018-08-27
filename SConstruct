import os
import platform

OS_NAME=platform.system()
SKIA_ROOT = os.path.normpath(os.getcwd())
SKIA_BUILD=os.path.join(SKIA_ROOT, 'build')
BIN_DIR=os.path.join(SKIA_BUILD, 'bin')
LIB_DIR=os.path.join(SKIA_BUILD, 'lib')

COMMON_CCFLAGS=''

os.environ['BIN_DIR'] = BIN_DIR;
os.environ['LIB_DIR'] = LIB_DIR;

OS_LIBPATH=[]
OS_CPPPATH=[]
OS_FLAGS='-g -Wall'
OS_SUBSYSTEM_CONSOLE=''
OS_SUBSYSTEM_WINDOWS=''
OS_LINKFLAGS=''
OS_LIBS=['SDL2']

if OS_NAME == 'Darwin':
  OS_LINKFLAGS='-framework OpenGL -framework CoreFoundation -framework  CoreGraphics -framework  IOKit -framework ApplicationServices'
  COMMON_CCFLAGS = COMMON_CCFLAGS + ' -D__APPLE__ -DHAS_PTHREAD -DMACOS'
  OS_LIBS = OS_LIBS + ['stdc++', 'pthread', 'm', 'dl']

elif OS_NAME == 'Linux':
  OS_LIBS = ['freetype', 'GL'] + OS_LIBS + ['stdc++', 'pthread', 'm', 'dl']
  COMMON_CCFLAGS = COMMON_CCFLAGS + ' -DLINUX -DHAS_PTHREAD'

elif OS_NAME == 'Windows':
  OS_LIBS=['SDL2', 'glad']
  OS_FLAGS='-DWIN32 -D_WIN32 -DWINDOWS /EHsc -D_CONSOLE  /DEBUG /Od /ZI'
  OS_LINKFLAGS='/MACHINE:X64 /DEBUG'
  OS_LIBPATH=[SKIA_ROOT+'/SDL2-2.0.7/lib/x64']
  OS_CPPPATH=[SKIA_ROOT+'/SDL2-2.0.7/']
  OS_SUBSYSTEM_CONSOLE='/SUBSYSTEM:CONSOLE  '
  OS_SUBSYSTEM_WINDOWS='/SUBSYSTEM:WINDOWS  '
  
LIBS=OS_LIBS

CCFLAGS=OS_FLAGS + COMMON_CCFLAGS 
CPPPATH=[
  SKIA_ROOT, 
  os.path.join(SKIA_ROOT, 'include'), 
  os.path.join(SKIA_ROOT, 'include/c'), 
  os.path.join(SKIA_ROOT, 'include/core'), 
  os.path.join(SKIA_ROOT, 'include/config'), 
  os.path.join(SKIA_ROOT, 'include/gpu'), 
  os.path.join(SKIA_ROOT, 'include/private'), 
  os.path.join(SKIA_ROOT, 'include/utils'), 
  os.path.join(SKIA_ROOT, 'include/utils/mac'), 
  os.path.join(SKIA_ROOT, 'include/ports'), 
  os.path.join(SKIA_ROOT, 'include/codec'), 
  os.path.join(SKIA_ROOT, 'include/image'), 
  os.path.join(SKIA_ROOT, 'include/effects'), 
  os.path.join(SKIA_ROOT, 'include/pathops'), 
  os.path.join(SKIA_ROOT, 'include/opts'), 
  os.path.join(SKIA_ROOT, 'include/sfnt'), 
  os.path.join(SKIA_ROOT, 'include/sksl'), 
  os.path.join(SKIA_ROOT, 'include/encode'), 
  os.path.join(SKIA_ROOT, 'include/shaders'), 
  os.path.join(SKIA_ROOT, 'include/pipe'), 
  os.path.join(SKIA_ROOT, 'include/atlastext'), 
  os.path.join(SKIA_ROOT, 'include/jumper'), 
  os.path.join(SKIA_ROOT, 'src/pdf'), 
  os.path.join(SKIA_ROOT, 'src/shaders'), 
  os.path.join(SKIA_ROOT, 'src/core'), 
  os.path.join(SKIA_ROOT, 'src/gpu'), 
  os.path.join(SKIA_ROOT, 'src/utils'), 
  os.path.join(SKIA_ROOT, 'src/codec'), 
  os.path.join(SKIA_ROOT, 'src/image'), 
  os.path.join(SKIA_ROOT, 'src/effects'), 
  os.path.join(SKIA_ROOT, 'src/pathops'), 
  os.path.join(SKIA_ROOT, 'src/opts'), 
  os.path.join(SKIA_ROOT, 'src/sfnt'), 
  os.path.join(SKIA_ROOT, 'src/sksl'), 
  os.path.join(SKIA_ROOT, 'src/encode'), 
  os.path.join(SKIA_ROOT, 'src/shaders'), 
  os.path.join(SKIA_ROOT, 'src/pipe'), 
  os.path.join(SKIA_ROOT, 'src/atlastext'), 
  os.path.join(SKIA_ROOT, 'src/jumper'), 
  os.path.join(SKIA_ROOT, 'modules/skshaper/include')
  ] + OS_CPPPATH

DefaultEnvironment(CCFLAGS = CCFLAGS, 
  CPPPATH = CPPPATH,
  LIBS=LIBS,
  LINKFLAGS=OS_LINKFLAGS,
  OS_SUBSYSTEM_CONSOLE=OS_SUBSYSTEM_CONSOLE,
  OS_SUBSYSTEM_WINDOWS=OS_SUBSYSTEM_WINDOWS,
  LIBPATH=[os.path.join(SKIA_BUILD, 'lib')] + OS_LIBPATH)

SConscriptFiles=[
  'SConscript'
  ]
  
SConscript(SConscriptFiles)

