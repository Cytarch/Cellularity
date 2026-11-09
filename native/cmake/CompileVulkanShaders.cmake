# Compiles and validates the Vulkan shader inventory for a host build.
#
# The same contract and the same tool invocations that the Android build uses are
# reproduced here, so the desktop port cannot drift from the mobile one: shader
# sources are inventoried against native/rendering/vulkan/vulkan-shader-contract.json,
# every declared stage is compiled, the result is validated, and the produced
# inventory is compared against the declared one. A stale extra .spv in the output
# directory fails the build rather than shipping.
#
# Required variables:
#   CELLULARITY_GLSL_SOURCE_DIR   directory holding the GLSL sources
#   CELLULARITY_SHADER_CONTRACT   path to vulkan-shader-contract.json
#   CELLULARITY_SHADER_OUTPUT_DIR where vulkan/shaders/<name>.<stage>.spv is written
#   CELLULARITY_GLSLC             glslc executable
#   CELLULARITY_SPIRV_VAL         spirv-val executable

cmake_minimum_required(VERSION 3.20)

foreach(required IN ITEMS
        CELLULARITY_GLSL_SOURCE_DIR
        CELLULARITY_SHADER_CONTRACT
        CELLULARITY_SHADER_OUTPUT_DIR
        CELLULARITY_GLSLC
        CELLULARITY_SPIRV_VAL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Shader compilation requires ${required}")
    endif()
endforeach()

foreach(tool IN ITEMS "${CELLULARITY_GLSLC}" "${CELLULARITY_SPIRV_VAL}")
    if(NOT EXISTS "${tool}")
        message(FATAL_ERROR "Shader tool is missing: ${tool}")
    endif()
endforeach()

if(NOT EXISTS "${CELLULARITY_SHADER_CONTRACT}")
    message(FATAL_ERROR "Shader contract is missing: ${CELLULARITY_SHADER_CONTRACT}")
endif()

file(READ "${CELLULARITY_SHADER_CONTRACT}" contractJson)
string(JSON targetEnvironment GET "${contractJson}" targetEnvironment)
string(JSON assetDirectory GET "${contractJson}" assetDirectory)

# CMake has no array-length operator, so the program count is found by probing
# until the index is out of range.
string(JSON programCount ERROR_VARIABLE probeError LENGTH "${contractJson}" programs)
if(programCount STREQUAL "NOTFOUND" OR NOT programCount MATCHES "^[0-9]+$")
    message(FATAL_ERROR
            "Shader contract has no programs array: ${probeError}")
endif()
if(programCount EQUAL 0)
    message(FATAL_ERROR "Shader contract declares no programs")
endif()

# The declared inventory: every stage of every program, as a path relative to the
# source directory, plus the relative output path it must produce.
set(expectedSources "")
set(compilePairs "")
set(expectedOutputs "")

math(EXPR lastProgram "${programCount} - 1")
foreach(index RANGE ${lastProgram})
    string(JSON programName GET "${contractJson}" programs ${index} name)
    foreach(stage IN ITEMS vertex fragment)
        string(JSON stageSource ERROR_VARIABLE stageError
                GET "${contractJson}" programs ${index} stages ${stage})
        if(stageSource STREQUAL "NOTFOUND" OR stageSource STREQUAL "")
            message(FATAL_ERROR
                    "Program ${programName} declares no ${stage} stage: ${stageError}")
        endif()

        set(absoluteSource "${CELLULARITY_GLSL_SOURCE_DIR}/${stageSource}")
        if(NOT EXISTS "${absoluteSource}")
            message(FATAL_ERROR
                    "Program ${programName} names a missing ${stage} source: "
                    "${absoluteSource}")
        endif()

        list(APPEND expectedSources "${stageSource}")
        list(APPEND compilePairs "${absoluteSource}|${stageSource}.spv")
        list(APPEND expectedOutputs "${stageSource}.spv")
    endforeach()
endforeach()

list(LENGTH expectedSources expectedSourceCount)

# Every file in the source directory must be declared, and every declared file must
# exist: an undeclared shader would never be compiled and a declared one that is
# absent would break the build much later, at first use.
file(GLOB_RECURSE actualSourcePaths
        RELATIVE "${CELLULARITY_GLSL_SOURCE_DIR}"
        "${CELLULARITY_GLSL_SOURCE_DIR}/*")
set(actualSources "")
foreach(path IN LISTS actualSourcePaths)
    string(REPLACE "\\" "/" normalized "${path}")
    list(APPEND actualSources "${normalized}")
endforeach()
list(SORT actualSources)
list(SORT expectedSources)

if(NOT actualSources STREQUAL expectedSources)
    set(missingSources "")
    foreach(source IN LISTS expectedSources)
        if(NOT source IN_LIST actualSources)
            list(APPEND missingSources "${source}")
        endif()
    endforeach()
    set(unexpectedSources "")
    foreach(source IN LISTS actualSources)
        if(NOT source IN_LIST expectedSources)
            list(APPEND unexpectedSources "${source}")
        endif()
    endforeach()
    message(FATAL_ERROR
            "Shader source inventory differs from its contract. "
            "missing=${missingSources} unexpected=${unexpectedSources}")
endif()

# Only files this contract declares are kept: a previous build's leftover would
# otherwise satisfy the output check while being stale.
file(REMOVE_RECURSE "${CELLULARITY_SHADER_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${CELLULARITY_SHADER_OUTPUT_DIR}/${assetDirectory}")

foreach(pair IN LISTS compilePairs)
    string(REPLACE "|" ";" fields "${pair}")
    list(GET fields 0 source)
    list(GET fields 1 relativeOutput)
    set(output "${CELLULARITY_SHADER_OUTPUT_DIR}/${assetDirectory}/${relativeOutput}")

    # --target-env, -O and -Werror match the Android build exactly, and both builds
    # read the same contract, so the two compile the same source the same way.
    #
    # They do not produce the same bytes, and that is expected rather than a defect.
    # This build uses glslc from the host toolchain while the Android build uses the
    # one inside the NDK, and two shaderc releases differ in their encoded generator
    # version and in the ids they assign; measured over this inventory, the modules
    # differ from byte 8 onwards and about a fifth of the bytes differ in total. The
    # contract therefore guarantees one source, one target environment and one set of
    # flags, not one byte image. Making the bytes match would mean compiling both
    # platforms with a single pinned shaderc, which is a decision about the toolchain
    # rather than about this file.
    execute_process(
            COMMAND "${CELLULARITY_GLSLC}" "--target-env=${targetEnvironment}" -O
                    -Werror "${source}" -o "${output}"
            RESULT_VARIABLE glslcResult
            ERROR_VARIABLE glslcError)
    if(NOT glslcResult EQUAL 0)
        message(FATAL_ERROR "glslc failed for ${source}:\n${glslcError}")
    endif()
    if(NOT EXISTS "${output}")
        message(FATAL_ERROR "glslc produced no output for ${source}")
    endif()
    file(SIZE "${output}" outputSize)
    if(outputSize EQUAL 0)
        message(FATAL_ERROR "glslc produced an empty module for ${source}")
    endif()

    execute_process(
            COMMAND "${CELLULARITY_SPIRV_VAL}" "--target-env" "${targetEnvironment}"
                    "${output}"
            RESULT_VARIABLE validatorResult
            ERROR_VARIABLE validatorError)
    if(NOT validatorResult EQUAL 0)
        message(FATAL_ERROR
                "spirv-val rejected ${relativeOutput}:\n${validatorError}")
    endif()
endforeach()

file(GLOB_RECURSE actualOutputPaths
        RELATIVE "${CELLULARITY_SHADER_OUTPUT_DIR}/${assetDirectory}"
        "${CELLULARITY_SHADER_OUTPUT_DIR}/${assetDirectory}/*")
set(actualOutputs "")
foreach(path IN LISTS actualOutputPaths)
    string(REPLACE "\\" "/" normalized "${path}")
    list(APPEND actualOutputs "${normalized}")
endforeach()
list(SORT actualOutputs)
list(SORT expectedOutputs)
if(NOT actualOutputs STREQUAL expectedOutputs)
    message(FATAL_ERROR
            "Generated shader inventory differs from its contract. "
            "expected=${expectedOutputs} actual=${actualOutputs}")
endif()

list(LENGTH actualOutputs compiledCount)
message(STATUS
        "Compiled and validated ${compiledCount} Vulkan shader modules "
        "(${targetEnvironment}) into ${CELLULARITY_SHADER_OUTPUT_DIR}/${assetDirectory}")
