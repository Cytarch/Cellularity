if(NOT DEFINED NM_TOOL OR NOT DEFINED CLIENT_ARCHIVE)
    message(FATAL_ERROR "NM_TOOL and CLIENT_ARCHIVE are required")
endif()

execute_process(
        COMMAND "${NM_TOOL}" -u "${CLIENT_ARCHIVE}"
        RESULT_VARIABLE NM_RESULT
        OUTPUT_VARIABLE UNDEFINED_SYMBOLS
        ERROR_VARIABLE NM_ERROR
)
if(NOT NM_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not inspect Vulkan backend client: ${NM_ERROR}")
endif()

foreach(FORBIDDEN_PATTERN IN ITEMS
        "(^|[^A-Za-z0-9_])vk[A-Z][A-Za-z0-9_]*"
        "ANativeWindow_[A-Za-z0-9_]+"
        "(^|[^A-Za-z0-9_])JNI[A-Za-z0-9_]*"
        "Java_com_cytarch_[A-Za-z0-9_]+"
)
    if(UNDEFINED_SYMBOLS MATCHES "${FORBIDDEN_PATTERN}")
        message(FATAL_ERROR
                "Main-side Vulkan ABI client has a forbidden dependency matching ${FORBIDDEN_PATTERN}:\n${UNDEFINED_SYMBOLS}"
        )
    endif()
endforeach()
