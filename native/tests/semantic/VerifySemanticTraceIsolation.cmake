if(NOT DEFINED NM_TOOL OR NM_TOOL STREQUAL "")
    message(FATAL_ERROR "CMAKE_NM is unavailable; semantic trace isolation cannot be checked")
endif()

foreach(archive IN ITEMS PUBLIC_ARCHIVE VERIFICATION_ARCHIVE)
    if(NOT DEFINED ${archive} OR NOT EXISTS "${${archive}}")
        message(FATAL_ERROR "Missing archive for semantic trace isolation: ${archive}")
    endif()
endforeach()

execute_process(
        COMMAND "${NM_TOOL}" -C "${PUBLIC_ARCHIVE}"
        RESULT_VARIABLE public_result
        OUTPUT_VARIABLE public_symbols
        ERROR_VARIABLE public_error
)
if(NOT public_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect public simulation archive: ${public_error}")
endif()
if(public_symbols MATCHES "SemanticTrace|emitSemanticTrace|ScopedSemanticTrace")
    message(FATAL_ERROR "Verification trace leaked into the public simulation archive")
endif()

execute_process(
        COMMAND "${NM_TOOL}" -C "${VERIFICATION_ARCHIVE}"
        RESULT_VARIABLE verification_result
        OUTPUT_VARIABLE verification_symbols
        ERROR_VARIABLE verification_error
)
if(NOT verification_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect verification simulation archive: ${verification_error}")
endif()
if(NOT verification_symbols MATCHES "emitSemanticTrace")
    message(FATAL_ERROR "Instrumented simulation archive does not contain the trace sink boundary")
endif()
