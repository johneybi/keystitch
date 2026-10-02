cmake_minimum_required(VERSION 3.16)
project(keystitch_pair_test LANGUAGES CXX)

# Standalone, record-only test tool: no GUI, device APIs, Qt or OpenSSL.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(PRODUCTION_SOURCES
    src/lib/barrier/ProtocolUtil.cpp
    src/lib/barrier/protocol_types.cpp
    src/lib/barrier/XBarrier.cpp
    src/lib/base/String.cpp
    src/lib/base/XBase.cpp
    src/lib/io/XIO.cpp)
set(CODEC_SOURCES)
set(SOURCE_DIGEST "")
foreach(source IN LISTS PRODUCTION_SOURCES)
    list(APPEND CODEC_SOURCES "${REPO_ROOT}/${source}")
    file(SHA256 "${REPO_ROOT}/${source}" digest)
    string(APPEND SOURCE_DIGEST "${source}:${digest};")
endforeach()
file(GLOB_RECURSE CODEC_HEADERS CONFIGURE_DEPENDS RELATIVE "${REPO_ROOT}"
    "${REPO_ROOT}/src/lib/barrier/*.h" "${REPO_ROOT}/src/lib/base/*.h"
    "${REPO_ROOT}/src/lib/io/*.h" "${REPO_ROOT}/src/lib/common/*.h"
    "${REPO_ROOT}/src/lib/arch/*.h")
list(SORT CODEC_HEADERS)
foreach(source IN LISTS CODEC_HEADERS)
    file(SHA256 "${REPO_ROOT}/${source}" digest)
    string(APPEND SOURCE_DIGEST "${source}:${digest};")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CODEC_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/codec.cpp")
foreach(source IN LISTS CODEC_HEADERS)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REPO_ROOT}/${source}")
endforeach()
foreach(source IN ITEMS codec.cpp CMakeLists.txt)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${source}" digest)
    string(APPEND SOURCE_DIGEST "${source}:${digest};")
endforeach()
string(SHA256 SOURCE_DIGEST "${SOURCE_DIGEST}")
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${REPO_ROOT}"
    OUTPUT_VARIABLE SOURCE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND git status --porcelain WORKING_DIRECTORY "${REPO_ROOT}"
    OUTPUT_VARIABLE SOURCE_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE)
if(SOURCE_STATUS)
    set(SOURCE_DIRTY 1)
else()
    set(SOURCE_DIRTY 0)
endif()
if(NOT SOURCE_COMMIT)
    set(SOURCE_COMMIT "unknown")
endif()
add_executable(pair-codec codec.cpp ${CODEC_SOURCES})
target_include_directories(pair-codec PRIVATE "${REPO_ROOT}/src/lib")
target_compile_features(pair-codec PRIVATE cxx_std_14)
target_compile_definitions(pair-codec PRIVATE NOLOGGING NDEBUG
    PAIR_SOURCE_COMMIT="${SOURCE_COMMIT}" PAIR_SOURCE_DIGEST="${SOURCE_DIGEST}"
    PAIR_SOURCE_DIRTY=${SOURCE_DIRTY})
if(WIN32)
    target_compile_definitions(pair-codec PRIVATE SYSAPI_WIN32 WINAPI_MSWINDOWS
        NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_definitions(pair-codec PRIVATE SYSAPI_UNIX WINAPI_XWINDOWS
        HAVE_SYS_SOCKET_H=1 HAVE_SYS_TYPES_H=1 HAVE_SOCKLEN_T=1)
endif()
