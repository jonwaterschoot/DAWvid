# cmake/dist.cmake
# Called by the 'dist' build target.  Assembles a self-contained installer
# folder at <build>/dist/DAWvid-install/.
#
# Required variables (passed via -D):
#   PLUGIN_FILE    — path to the built DAWvid.clap
#   FFMPEG_ROOT    — root of the FFmpeg dev package
#   INSTALLER_DIR  — project source installer/ directory
#   DIST_DIR       — destination directory to populate
#   BITWIG_DIR     — project source bitwig/ directory (optional, for controller script)

foreach(var PLUGIN_FILE FFMPEG_ROOT INSTALLER_DIR DIST_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "dist.cmake: missing required variable ${var}")
    endif()
endforeach()

# ── Create output directory ───────────────────────────────────────────────────
file(MAKE_DIRECTORY "${DIST_DIR}")

# ── Plugin ────────────────────────────────────────────────────────────────────
file(COPY "${PLUGIN_FILE}" DESTINATION "${DIST_DIR}")
message(STATUS "  + ${PLUGIN_FILE}")

# ── FFmpeg runtime DLLs ───────────────────────────────────────────────────────
foreach(lib avcodec avformat avutil swresample swscale)
    file(GLOB _matches "${FFMPEG_ROOT}/bin/${lib}-*.dll")
    if(NOT _matches)
        message(WARNING "dist.cmake: no DLL found for ${lib} in ${FFMPEG_ROOT}/bin")
    endif()
    foreach(dll ${_matches})
        file(COPY "${dll}" DESTINATION "${DIST_DIR}")
        message(STATUS "  + ${dll}")
    endforeach()
endforeach()

# ── Installer scripts ─────────────────────────────────────────────────────────
file(COPY "${INSTALLER_DIR}/install.ps1"       DESTINATION "${DIST_DIR}")
file(COPY "${INSTALLER_DIR}/install.bat"       DESTINATION "${DIST_DIR}")
file(COPY "${INSTALLER_DIR}/INSTALL-README.txt" DESTINATION "${DIST_DIR}")
message(STATUS "  + install.bat / install.ps1 / INSTALL-README.txt")

# ── Bitwig controller script (optional) ──────────────────────────────────────
if(DEFINED BITWIG_DIR)
    set(_ctrl "${BITWIG_DIR}/DAWvid.control.js")
    if(EXISTS "${_ctrl}")
        file(COPY "${_ctrl}" DESTINATION "${DIST_DIR}")
        message(STATUS "  + DAWvid.control.js")
    else()
        message(STATUS "  (DAWvid.control.js not found in ${BITWIG_DIR}, skipping)")
    endif()
endif()

message(STATUS "Distribution package ready: ${DIST_DIR}")
