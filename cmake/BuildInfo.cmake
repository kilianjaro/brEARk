# Writes BuildInfo.h with the git commit and build time, so the debug view
# proves which build the host has loaded. Runs on every build; the header is
# only rewritten when its content changes (at most once a minute).
execute_process (COMMAND git rev-parse --short HEAD
                 WORKING_DIRECTORY "${SOURCE_DIR}"
                 OUTPUT_VARIABLE hash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

execute_process (COMMAND git status --porcelain --untracked-files=no
                 WORKING_DIRECTORY "${SOURCE_DIR}"
                 OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

if (dirty)
    set (hash "${hash}+") # uncommitted changes
endif()

string (TIMESTAMP now "%Y-%m-%d %H:%M")
set (content "#pragma once\n#define BREARK_BUILD_ID \"${hash} ${now}\"\n")

if (EXISTS "${OUTPUT}")
    file (READ "${OUTPUT}" existing)
endif()

if (NOT "${existing}" STREQUAL "${content}")
    file (WRITE "${OUTPUT}" "${content}")
endif()
