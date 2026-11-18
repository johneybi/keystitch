# Build-time identity, refreshed before compiling common. Dirty/unknown source
# remains usable for development, but the release verifier rejects it.
cmake_minimum_required(VERSION 3.21)
set(source_revision "unknown")
set(source_dirty true)
find_program(git_executable git)
if(git_executable)
    execute_process(COMMAND "${git_executable}" -C "${SOURCE_ROOT}" rev-parse HEAD
        OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE revision_status)
    execute_process(COMMAND "${git_executable}" -C "${SOURCE_ROOT}" status --porcelain
        --untracked-files=normal --ignore-submodules=none
        OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE dirty_status)
    if(revision_status EQUAL 0 AND revision MATCHES "^[0-9a-f]+$")
        set(source_revision "${revision}")
        if(dirty_status EQUAL 0 AND dirty STREQUAL "")
            set(source_dirty false)
        endif()
    endif()
endif()

function(json_quote output value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\r" "\\r" value "${value}")
    string(REPLACE "\t" "\\t" value "${value}")
    set(${output} "\"${value}\"" PARENT_SCOPE)
endfunction()

set(metadata "{\"format\":\"KeyStitchBuildInfo:v1\",\"schemaVersion\":1,\"sourceDirty\":${source_dirty}")
foreach(pair IN ITEMS "sourceRevision;${source_revision}" "buildId;${BUILD_ID}"
        "version;${PRODUCT_VERSION}" "platform;${TARGET_PLATFORM}"
        "architecture;${TARGET_ARCH}" "compiler;${COMPILER_ID}"
        "compilerVersion;${COMPILER_VERSION}" "minimumMacOS;${MINIMUM_MACOS}")
    list(GET pair 0 name)
    list(LENGTH pair count)
    set(value "")
    if(count GREATER 1)
        list(SUBLIST pair 1 -1 value)
    endif()
    json_quote(quoted "${value}")
    string(APPEND metadata ",\"${name}\":${quoted}")
endforeach()
string(APPEND metadata "}")
json_quote(cpp_literal "${metadata}")
file(WRITE "${OUTPUT_FILE}.tmp" "// Generated; do not edit.\n#define KEYSTITCH_BUILD_METADATA ${cpp_literal}\n")
configure_file("${OUTPUT_FILE}.tmp" "${OUTPUT_FILE}" COPYONLY)
