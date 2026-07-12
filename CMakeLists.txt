## This file is a quick tutorial on writing CMakeLists for targeting the Vita
cmake_minimum_required(VERSION 3.10)

## This includes the Vita toolchain, must go before project definition
# It is a convenience so you do not have to type
# -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake for cmake. It is
# highly recommended that you include this block for all projects.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()
## Define project parameters here
# Name of the project
project(zenonia_2)
# This line adds Vita helper macros, must go after project definition in order
# to build Vita specific artifacts (self/vpk).
include("${VITASDK}/share/vita.cmake" REQUIRED)

## Configuration options for this app
# Display name (under bubble in LiveArea)
set(VITA_APP_NAME "Zenonia 2")
# Unique ID must be exactly 9 characters. Recommended: XXXXYYYYY where X =
# unique string of developer and Y = a unique number for this app
set(VITA_TITLEID  "PSVZ00002")
# Optional version string to show in LiveArea's more info screen
set(VITA_VERSION  "01.00")

## Currently unused by loader/so_util.c (reverted to the original, non-kubridge
## memory strategy after the Vita3K vitaGL crash turned out to be emulator
## session instability, not a memory-layout bug -- see port_progress.md §9.4).
## Kept as a no-op flag in case a Vita3K-specific path is reintroduced later.
## Real hardware is the default target now.
option(EMULATOR_BUILD "Target Vita3K instead of real hardware (currently unused, see comment above)" OFF)
if (EMULATOR_BUILD)
  add_definitions(-DEMULATOR_BUILD)
endif()

## Verbose JNI tracing (every GetMethodID/GetStaticMethodID lookup and
## dispatch). OFF by default -- Prince of Persia's Fixes_Log.md #12/13
## documents this exact setting (FALSOJNI_DEBUGLEVEL=0) making the game
## unplayably slow, since every JNI call pays a full file open+write+close.
## Turn ON only to debug a specific boot issue, then back OFF.
option(ENABLE_VERBOSE_JNI_LOG "Log every FalsoJNI call (debug only, slow)" ON)
if (ENABLE_VERBOSE_JNI_LOG)
  add_definitions(-DFALSOJNI_DEBUGLEVEL=0)
endif()

## Shader de post-proceso (sharpen) opcional sobre el blit final del motor
## (compositor por-software 400x240 -> pantalla completa, ver
## port_progress.md Backlog B.1). OFF por defecto: el .vpk resultante es
## BIT-A-BIT equivalente al build ya confirmado en consola (postprocess.c
## queda en no-op, ver loader/postprocess.c). Compilar con esta opcion en ON
## en un build/VPK aparte (build.sh acepta un 2do argumento "shader") para
## poder comparar contra el build normal sin arriesgar la version ya probada.
option(ENABLE_POSTPROCESS_SHADER "Apply an experimental sharpen post-process shader to the engine's software-composited frame (see port_progress.md Backlog B.1)" OFF)
if (ENABLE_POSTPROCESS_SHADER)
  add_definitions(-DPOSTPROCESS_SHADER)
  set(VPK_SUFFIX "_shader")
else()
  set(VPK_SUFFIX "")
endif()

## Option to build a safe VPK without copyrighted assets
option(SAFE_DISTRIBUTION "Build VPK without copyrighted assets for safe distribution" OFF)
if (SAFE_DISTRIBUTION)
  set(VPK_SUFFIX "_safe")
endif()

## Flags and includes for building
# Note that we make sure not to overwrite previous flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
# Optional. You can specify more param.sfo flags this way.
# ATTRIBUTE2=12 requests the extended/high memory budget. Without it, vitaGL's
# SceShaccCg (Cg shader compiler) initialization deterministically null-derefs
# inside Vita3K's call_import while starting the module -- confirmed by
# comparing against Prince of Persia's working Vita3K config, which sets this.
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1 -d ATTRIBUTE2=12")
# Add any additional include paths here
include_directories(
  ./common # This is for debugScreenPrintf(), you shouldn't need it in your projects
  ./lib    # for <falso_jni/FalsoJNI.h>
)

# Add any additional library paths here
# ${CMAKE_CURRENT_BINARY_DIR} lets you use any library currently being built
link_directories(
  ${CMAKE_CURRENT_BINARY_DIR}
)

## Build and link
# Add all the files needed to compile here
add_executable(${PROJECT_NAME}
  ./loader/main.c
  ./loader/dynlib.c
  ./loader/so_util.c
  ./loader/java.c
  ./loader/audio.c
  ./loader/postprocess.c
  ./common/debugScreen.c
  ./lib/falso_jni/FalsoJNI.c
  ./lib/falso_jni/FalsoJNI_ImplBridge.c
  ./lib/falso_jni/FalsoJNI_Logger.c
)

# Library to link to (drop the -l prefix).
target_link_libraries(${PROJECT_NAME}
  vitaGL
  vitashark
  mathneon
  m
  # Decodificador OGG Vorbis (Tremor, punto fijo) para loader/audio.c
  vorbisidec
  ogg
  -Wl,--whole-archive pthread -Wl,--no-whole-archive
  SceDisplay_stub
  SceCtrl_stub
  SceTouch_stub
  SceAudio_stub
  SceAudioIn_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceSysmodule_stub
  ScePower_stub
  SceGxm_stub
  SceShaccCg_stub
  SceShaccCgExt
  SceCommonDialog_stub
  SceKernelDmacMgr_stub
  SceLibKernel_stub
  taihen_stub
  kubridge_stub
)

## Create Vita files
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE NOASLR)
# The FILE directive lets you add additional files to the VPK, the syntax is
# FILE src_path dst_path_in_vpk. In this case, we add the LiveArea paths.

if(SAFE_DISTRIBUTION)
  vita_create_vpk(${PROJECT_NAME}${VPK_SUFFIX}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
  )
else()
  vita_create_vpk(${PROJECT_NAME}${VPK_SUFFIX}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    FILE sce_sys/icon0.png sce_sys/icon0.png
    FILE sce_sys/pic0.png sce_sys/pic0.png
    FILE sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
    FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
    # Splash/titulo en crudo (960x544 RGBA8888, generados desde logo.png/title.png
    # del APK original, escalados a pantalla completa) que el loader muestra en
    # vez de la pantalla blanca de logo/titulo (ver splash_* en main.c). touch.rgba
    # (310x30) es el aviso "toca para continuar" que Android parpadeaba sobre el
    # titulo (Zenonia2UIControllerView.showTouchViewAnim).
    FILE splash.rgba splash.rgba
    FILE title.rgba title.rgba
    FILE touch.rgba touch.rgba
  )
endif()