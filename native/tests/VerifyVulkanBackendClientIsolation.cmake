# Vulkan backend client isolation.
#
# The client-side ABI library must not reference Vulkan, Android or JNI. The check
# reads the archive's undefined symbols: a forbidden dependency appears there as
# an unresolved external.
#
# Symbol inspection goes through a portable helper because MSVC has no nm: see
# tests/cmake/ArchiveSymbols.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/cmake/ArchiveSymbols.cmake")

if(NOT DEFINED NM_TOOL OR NOT DEFINED CLIENT_ARCHIVE)
    message(FATAL_ERROR "NM_TOOL and CLIENT_ARCHIVE are required")
endif()

cellularity_list_archive_symbols(
        "${NM_TOOL}" "${CLIENT_ARCHIVE}" undefined_symbols error UNDEFINED_ONLY)
if(NOT error STREQUAL "")
    message(FATAL_ERROR "${error}")
endif()

# An empty list means the archive references nothing external at all, which
# satisfies the isolation requirement by inspection.
foreach(forbidden_pattern IN ITEMS
        "(^|[^A-Za-z0-9_])vk[A-Z][A-Za-z0-9_]*"
        "ANativeWindow_[A-Za-z0-9_]+"
        "(^|[^A-Za-z0-9_])JNI[A-Za-z0-9_]*"
        "Java_com_cytarch_[A-Za-z0-9_]+")
    # Filter rather than scan every pair; see tests/cmake/ArchiveSymbols.cmake.
    set(offenders "${undefined_symbols}")
    list(FILTER offenders INCLUDE REGEX "${forbidden_pattern}")
    if(offenders)
        list(GET offenders 0 first_offender)
        message(FATAL_ERROR
                "Main-side Vulkan ABI client has a forbidden dependency matching ${forbidden_pattern}: ${first_offender}")
    endif()
endforeach()
