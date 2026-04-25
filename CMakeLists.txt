cmake_minimum_required(VERSION 3.21)
project(DAWvid VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(BUILD_STANDALONE "Build a standalone test host" OFF)

# ── Dependencies (via FetchContent) ──────────────────────────────────────────
include(FetchContent)

# CLAP SDK
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(clap)

# clap-helpers (optional but very useful)
FetchContent_Declare(
    clap_helpers
    GIT_REPOSITORY https://github.com/free-audio/clap-helpers.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(clap_helpers)

# GLAD 2 (OpenGL extension loader) — Windows needs runtime function loading
if(WIN32)
    FetchContent_Declare(
        glad2
        GIT_REPOSITORY https://github.com/Dav1dde/glad.git
        GIT_TAG        v2.0.6
        SOURCE_SUBDIR  cmake
    )
    FetchContent_MakeAvailable(glad2)
    glad_add_library(glad_gl_core_33 REPRODUCIBLE API gl:core=3.3)
endif()

# ── Find FFmpeg ───────────────────────────────────────────────────────────────
if(WIN32)
    # Windows: use a pre-built dev package; caller sets -DFFMPEG_ROOT=<path>
    if(NOT DEFINED FFMPEG_ROOT)
        message(FATAL_ERROR
            "On Windows set -DFFMPEG_ROOT=<path> to an FFmpeg dev package "
            "(headers in <path>/include, import libs in <path>/lib). "
            "Download from https://github.com/BtbN/FFmpeg-Builds/releases")
    endif()
    # MSVC import libs generated from the DLLs so delay-load works correctly.
    # MinGW-format libs (in FFMPEG_ROOT/lib) link fine but MSVC cannot delay-load them.
    set(FFMPEG_MSVC_LIBS "${FFMPEG_ROOT}/../ffmpeg-msvc-libs"
        CACHE PATH "Directory containing MSVC-format FFmpeg import libs")
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    target_include_directories(FFmpeg::FFmpeg INTERFACE "${FFMPEG_ROOT}/include")
    foreach(_lib avcodec-62 avformat-62 avutil-60 swresample-6 swscale-9)
        find_library(_ff_${_lib} ${_lib}
            PATHS "${FFMPEG_MSVC_LIBS}" NO_DEFAULT_PATH REQUIRED)
        target_link_libraries(FFmpeg::FFmpeg INTERFACE ${_ff_${_lib}})
    endforeach()
    set(FFMPEG_TARGET FFmpeg::FFmpeg)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec
        libavformat
        libavutil
        libswscale
    )
    set(FFMPEG_TARGET PkgConfig::FFMPEG)
endif()

find_package(OpenGL REQUIRED)

# Platform-specific windowing
if(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
elseif(WIN32)
    find_package(OpenGL REQUIRED)
    # WGL is included with OpenGL on Windows
elseif(UNIX)
    find_package(X11 REQUIRED)
    find_package(OpenGL REQUIRED)
    # GLX comes with OpenGL on Linux
endif()

# ── Plugin sources ────────────────────────────────────────────────────────────
set(PLUGIN_SOURCES
    src/plugin_entry.cpp
    src/video_player_plugin.cpp
    src/video_decoder.cpp
    src/gl_renderer.cpp
    src/gui_window.cpp
    src/transport_sync.cpp
    src/ipc_bridge.cpp
)

set(PLUGIN_HEADERS
    include/video_player_plugin.h
    include/video_decoder.h
    include/gl_renderer.h
    include/gui_window.h
    include/transport_sync.h
    include/ipc_bridge.h
    include/plugin_info.h
)

# ── Build the CLAP shared library ─────────────────────────────────────────────
add_library(DAWvid MODULE ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})

target_include_directories(DAWvid PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(DAWvid PRIVATE
    clap
    clap-helpers
    ${FFMPEG_TARGET}
    OpenGL::GL
)

if(APPLE)
    target_link_libraries(DAWvid PRIVATE
        ${COCOA_FRAMEWORK}
        ${OPENGL_FRAMEWORK}
    )
    set_target_properties(DAWvid PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION clap
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    )
elseif(WIN32)
    target_link_libraries(DAWvid PRIVATE opengl32 gdi32 user32 glad_gl_core_33 delayimp ws2_32)
    # Delay-load FFmpeg so entry_init() can call SetDllDirectoryA() before any
    # FFmpeg symbol is resolved. Names must match the actual runtime DLL filenames.
    set(FFMPEG_DELAY_DLLS "avcodec-62.dll;avformat-62.dll;avutil-60.dll;swscale-9.dll"
        CACHE STRING "FFmpeg DLL filenames for delay-loading (must match installed .dll names)")
    foreach(_dll ${FFMPEG_DELAY_DLLS})
        target_link_options(DAWvid PRIVATE /DELAYLOAD:${_dll})
    endforeach()
    set_target_properties(DAWvid PROPERTIES SUFFIX ".clap")
elseif(UNIX)
    target_link_libraries(DAWvid PRIVATE
        ${X11_LIBRARIES}
        ${OPENGL_gl_LIBRARY}
        GL
        X11
    )
    set_target_properties(DAWvid PROPERTIES
        PREFIX ""
        SUFFIX ".clap"
    )
endif()

# ── Install ───────────────────────────────────────────────────────────────────
if(APPLE)
    set(CLAP_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
elseif(WIN32)
    # COMMONPROGRAMW6432 always resolves to the 64-bit Common Files directory
    # on Windows x64, regardless of whether cmake itself is a 32-bit process.
    if(DEFINED ENV{COMMONPROGRAMW6432})
        set(CLAP_INSTALL_DIR "$ENV{COMMONPROGRAMW6432}/CLAP")
    else()
        set(CLAP_INSTALL_DIR "$ENV{COMMONPROGRAMFILES}/CLAP")
    endif()
elseif(UNIX)
    set(CLAP_INSTALL_DIR "$ENV{HOME}/.clap")
endif()

install(TARGETS DAWvid
    # Windows treats MODULE targets as RUNTIME (DLL platform); non-Windows as LIBRARY.
    RUNTIME DESTINATION "${CLAP_INSTALL_DIR}/DAWvid"
    LIBRARY DESTINATION "${CLAP_INSTALL_DIR}/DAWvid"
    BUNDLE  DESTINATION "${CLAP_INSTALL_DIR}/DAWvid"
)

# Convenience target: build + copy the .clap directly, bypassing cmake --install
# so FetchContent dependency install rules (clap SDK headers etc.) don't run
# and fail writing to CMAKE_INSTALL_PREFIX.
add_custom_target(install-plugin
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CLAP_INSTALL_DIR}/DAWvid"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:DAWvid>"
        "${CLAP_INSTALL_DIR}/DAWvid/DAWvid.clap"
    DEPENDS DAWvid
    COMMENT "Installing plugin to ${CLAP_INSTALL_DIR}/DAWvid"
)

# ── Distribution package (Windows) ───────────────────────────────────────────
if(WIN32)
    set(DIST_DIR "${CMAKE_BINARY_DIR}/dist/DAWvid-install")
    add_custom_target(dist
        COMMAND ${CMAKE_COMMAND}
            "-DPLUGIN_FILE=$<TARGET_FILE:DAWvid>"
            "-DFFMPEG_ROOT=${FFMPEG_ROOT}"
            "-DINSTALLER_DIR=${CMAKE_SOURCE_DIR}/installer"
            "-DBITWIG_DIR=${CMAKE_SOURCE_DIR}/bitwig"
            "-DDIST_DIR=${DIST_DIR}"
            -P "${CMAKE_SOURCE_DIR}/cmake/dist.cmake"
        DEPENDS DAWvid
        COMMENT "Assembling installer package in ${DIST_DIR}"
    )
endif()
