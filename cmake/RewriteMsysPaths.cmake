# FFmpeg's configure runs under an MSYS shell on Windows and records the source and install directories as
# /d/... style paths. A native make cannot resolve those, so the generated makefiles are rewritten to drive paths
# right after configuration.
#
# Usage: cmake -Ddirectory=<ffmpeg build dir> -P RewriteMsysPaths.cmake

if (NOT DEFINED directory)
    message(FATAL_ERROR "RewriteMsysPaths.cmake needs -Ddirectory=<ffmpeg build dir>")
endif()

file(GLOB_RECURSE makefiles
    "${directory}/Makefile"
    "${directory}/*.mak"
)

set(rewritten 0)

foreach (makefile IN LISTS makefiles)
    file(READ "${makefile}" contents)
    string(REGEX REPLACE "/([a-zA-Z])/" "\\1:/" fixed "${contents}")

    if (NOT fixed STREQUAL contents)
        file(WRITE "${makefile}" "${fixed}")
        math(EXPR rewritten "${rewritten} + 1")
    endif()
endforeach()

message(STATUS "FFmpeg: rewrote ${rewritten} makefiles to native paths")
