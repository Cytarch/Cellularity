# Portable archive symbol listing for the isolation tests.
#
# The isolation checks assert that a symbol does not appear in an archive. GNU
# toolchains answer that with `nm`; MSVC has no nm and answers it with
# `dumpbin /SYMBOLS`. This module hides the difference behind one function and
# returns cleaned symbol names, so each caller keeps a single substring test.
#
# Returned names are the raw mangled names on every toolchain. Both Itanium
# (`_ZN11cellularity12verification17emitSemanticTraceE...`) and MSVC
# (`?emitSemanticTrace@...`) mangling keep each identifier as a contiguous
# substring, so substring matching stays valid. Demangled output is avoided on
# purpose: `nm -C` prints `::`, spaces and parentheses, which the per-line
# cleanup below would cut at, reducing every namespaced symbol to its first
# namespace and making absence checks pass vacuously.

# cellularity_list_archive_symbols(<tool> <archive> <out_var> <error_var>
#                                   [UNDEFINED_ONLY])
#
# Sets <out_var> to the semicolon-separated symbol names and <error_var> to an
# empty string on success, or to a diagnostic message on failure.
#
# With UNDEFINED_ONLY, only symbols the archive references but does not define
# are returned, which is how the Vulkan client isolation check proves that the
# client has no Vulkan, Android or JNI dependency.
cmake_policy(SET CMP0007 NEW)

function(cellularity_list_archive_symbols nm_tool archive out_var error_var)
    set(options UNDEFINED_ONLY)
    cmake_parse_arguments(ARG "${options}" "" "" ${ARGN})

    set(${out_var} "" PARENT_SCOPE)
    set(${error_var} "" PARENT_SCOPE)

    if(NOT DEFINED nm_tool OR nm_tool STREQUAL "")
        set(${error_var} "No symbol inspection tool is configured" PARENT_SCOPE)
        return()
    endif()

    get_filename_component(tool_name "${nm_tool}" NAME_WE)
    string(TOLOWER "${tool_name}" tool_name_lower)
    if(tool_name_lower STREQUAL "dumpbin")
        set(use_dumpbin TRUE)
    else()
        set(use_dumpbin FALSE)
    endif()

    if(use_dumpbin)
        # One invocation serves both modes: dumpbin has no "/DEFINED" option
        # (LNK4044), and an undefined external is identified by its section field
        # reading UNDEF, which is parsed below.
        execute_process(
                COMMAND "${nm_tool}" /SYMBOLS "${archive}"
                RESULT_VARIABLE listed
                OUTPUT_VARIABLE listing
                ERROR_VARIABLE listing_error
        )
    else()
        if(ARG_UNDEFINED_ONLY)
            execute_process(
                    COMMAND "${nm_tool}" -u "${archive}"
                    RESULT_VARIABLE listed
                    OUTPUT_VARIABLE listing
                    ERROR_VARIABLE listing_error
            )
        else()
            execute_process(
                    COMMAND "${nm_tool}" "${archive}"
                    RESULT_VARIABLE listed
                    OUTPUT_VARIABLE listing
                    ERROR_VARIABLE listing_error
            )
        endif()
    endif()

    if(NOT listed EQUAL 0)
        set(${error_var}
                "Could not inspect archive ${archive}: ${listing_error}"
                PARENT_SCOPE)
        return()
    endif()

    # Normalise line endings first: dumpbin emits CRLF pairs.
    string(REPLACE "\r\n" "\n" listing "${listing}")
    string(REPLACE "\r" "\n" listing "${listing}")

    # Reduce the listing to one symbol name per line.
    #
    # CMake regular expressions have no multiline mode: '^' and '$' anchor to the
    # whole subject, not to each line. The record structure is therefore removed
    # by deleting the leading fields and the trailing whitespace instead of
    # matching whole lines, and the vertical bar is cut with both sides before and
    # after it so the bare name remains.
    if(use_dumpbin)
        # Records look like:
        #   "002 00000000 SECT1  notype Static | .drectve"
        #   "00A 00000000 UNDEF  notype External | ?foo@@YAXXZ"
        # The section field carries UNDEF for an undefined external.
        if(ARG_UNDEFINED_ONLY)
            set(prefix_pattern "[ \t]*[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+UNDEF[ \t]+[^|\r\n]*\\|[ \t]*")
        else()
            set(prefix_pattern "[ \t]*[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+[A-Za-z0-9_]+[ \t]+[^|\r\n]*\\|[ \t]*")
        endif()
        string(REGEX REPLACE "${prefix_pattern}" "" symbol_block "${listing}")
    else()
        # nm output: "<hex-address-or-blank> <type-letter> <name>".
        if(ARG_UNDEFINED_ONLY)
            string(REGEX REPLACE "[ \t]*[0-9A-Fa-f]*[ \t]+[Uu][ \t]+" "" symbol_block "${listing}")
        else()
            string(REGEX REPLACE "[ \t]*[0-9A-Fa-f]*[ \t]+[A-Za-z][ \t]+" "" symbol_block "${listing}")
        endif()
    endif()

    # Reduce every line to just its symbol name, leaving other lines empty.
    #
    # Back-references are deliberately avoided: CMake's string(REGEX REPLACE)
    # interprets \1 in the replacement string as an escape sequence and strips it,
    # so a capture-based rewrite silently deletes the text instead of re-emitting
    # it. Deleting the surrounding text is an equivalent rewrite that needs no
    # back-reference.
    string(REGEX REPLACE "[^A-Za-z0-9_@?$.\n][^\n]*" "\n" symbol_block "${symbol_block}")
    string(REGEX REPLACE "[ \t\r]+" "\n" symbol_block "${symbol_block}")
    string(REPLACE "\n" ";" symbols "${symbol_block}")
    list(REMOVE_ITEM symbols "")
    # A line that was not a symbol record keeps its leading text only if that text
    # was already symbol-shaped; drop the known non-symbol headings explicitly.
    list(FILTER symbols EXCLUDE REGEX "^(Microsoft|Copyright|Dump|File|COFF|Section|LIBRARY|LINK|warning|Summary|SECTION|RELOCATIONS)$")

    if(NOT symbols)
        # An empty undefined-symbol list is a legitimate result: an archive with
        # no external references is exactly what the client isolation wants.
        if(ARG_UNDEFINED_ONLY)
            set(${out_var} "" PARENT_SCOPE)
            return()
        endif()
        set(${error_var}
                "Symbol listing for ${archive} produced no symbols; the check would pass vacuously"
                PARENT_SCOPE)
        return()
    endif()

    set(${out_var} "${symbols}" PARENT_SCOPE)
endfunction()

# cellularity_assert_patterns_absent(<tool> <archive> <patterns> <label>)
#
# Fails the surrounding -P script when any pattern appears among the archive's
# symbols. <patterns> is a semicolon-separated list.
function(cellularity_assert_patterns_absent nm_tool archive patterns label)
    cellularity_list_archive_symbols("${nm_tool}" "${archive}" symbols error)
    if(NOT error STREQUAL "")
        message(FATAL_ERROR "${error}")
    endif()

    foreach(pattern IN LISTS patterns)
        # Filter instead of scanning in a nested loop: an archive can hold tens of
        # thousands of symbols, and a CMake-level comparison per pair is slow
        # enough to dominate the test runtime.
        set(offenders "${symbols}")
        list(FILTER offenders INCLUDE REGEX "${pattern}")
        if(offenders)
            list(GET offenders 0 first_offender)
            list(LENGTH offenders offender_count)
            message(FATAL_ERROR
                    "${label}: pattern '${pattern}' matched ${offender_count} symbol(s) in ${archive}, first is '${first_offender}'")
        endif()
    endforeach()
endfunction()
