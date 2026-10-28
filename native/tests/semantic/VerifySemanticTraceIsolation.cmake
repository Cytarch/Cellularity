# Semantic trace isolation.
#
# The instrumented simulation archive carries the semantic trace; the public
# archive must not. The trace machinery is compiled in only when
# CELLULARITY_SEMANTIC_TRACE is defined, so this check proves the split held.
#
# Symbol inspection goes through a portable helper because MSVC has no nm: see
# tests/cmake/ArchiveSymbols.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/ArchiveSymbols.cmake")

if(NOT DEFINED NM_TOOL OR NM_TOOL STREQUAL "")
    message(FATAL_ERROR
            "No symbol inspection tool is configured; semantic trace isolation cannot be checked")
endif()

foreach(archive IN ITEMS PUBLIC_ARCHIVE VERIFICATION_ARCHIVE)
    if(NOT DEFINED ${archive} OR NOT EXISTS "${${archive}}")
        message(FATAL_ERROR "Missing archive for semantic trace isolation: ${archive}")
    endif()
endforeach()

cellularity_assert_patterns_absent(
        "${NM_TOOL}" "${PUBLIC_ARCHIVE}"
        "SemanticTrace;emitSemanticTrace;ScopedSemanticTrace"
        "Verification trace leaked into the public simulation archive")

cellularity_list_archive_symbols("${NM_TOOL}" "${VERIFICATION_ARCHIVE}" verification_symbols error)
if(NOT error STREQUAL "")
    message(FATAL_ERROR "${error}")
endif()

set(found_trace_sink FALSE)
foreach(symbol IN LISTS verification_symbols)
    if(symbol MATCHES "emitSemanticTrace")
        set(found_trace_sink TRUE)
        break()
    endif()
endforeach()
if(NOT found_trace_sink)
    message(FATAL_ERROR
            "Instrumented simulation archive does not contain the trace sink boundary")
endif()
