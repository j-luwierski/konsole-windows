# SPDX-License-Identifier: GPL-2.0-or-later
function(konsole_generate_completion_header output_directory)
    set(source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KonsoleCompletion.ps1")
    file(READ "${source}" KONSOLE_COMPLETION_SCRIPT)
    # Remove full-line comments and indentation, keeping statement newlines intact.
    string(REGEX REPLACE "(^|\n)[ \t]*#[^\n]*" "\\1" KONSOLE_COMPLETION_SCRIPT "${KONSOLE_COMPLETION_SCRIPT}")
    string(REGEX REPLACE "(^|\n)[ \t]+" "\\1" KONSOLE_COMPLETION_SCRIPT "${KONSOLE_COMPLETION_SCRIPT}")
    string(REGEX REPLACE "\n\n+" "\n" KONSOLE_COMPLETION_SCRIPT "${KONSOLE_COMPLETION_SCRIPT}")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/windowscompletionscript.h.in"
                   "${output_directory}/windowscompletionscript.h" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
endfunction()
