# InputLeap -- mouse and keyboard sharing utility
# Copyright (C) 2012-2016 Symless Ltd.
# Copyright (C) 2009 Nick Bolton
#
# This package is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# found in the file LICENSE that should have accompanied this file.
#
# This package is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

file(GLOB headers "*.h")
file(GLOB sources "*.cpp")

if (WIN32)
    file(GLOB arch_headers "win32/*.h")
    file(GLOB arch_sources "win32/*.cpp")
elseif (UNIX)
    file(GLOB arch_headers "unix/*.h")
    file(GLOB arch_sources "unix/*.cpp")
endif()

list(APPEND headers ${arch_headers})
list(APPEND sources ${arch_sources})

if(INPUTLEAP_ADD_HEADERS)
    list(APPEND sources ${headers})
endif()

add_library(common STATIC ${sources})

# New identity per configure, shared by all executables linked from this tree.
# Refresh source state at build time so editing a .cpp after configure cannot
# silently keep a clean-source claim. Copy only when metadata actually changes.
string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef keystitch_build_id)
set(keystitch_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(CMAKE_GENERATOR_PLATFORM)
    set(keystitch_arch "${CMAKE_GENERATOR_PLATFORM}")
elseif(CMAKE_OSX_ARCHITECTURES)
    string(REPLACE ";" "," keystitch_arch "${CMAKE_OSX_ARCHITECTURES}")
endif()
add_custom_target(refresh_build_info
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
        "-DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/BuildInfoGenerated.h"
        "-DBUILD_ID=${keystitch_build_id}"
        "-DPRODUCT_VERSION=${INPUTLEAP_VERSION_MAJOR}.${INPUTLEAP_VERSION_MINOR}.${INPUTLEAP_VERSION_PATCH}"
        "-DTARGET_PLATFORM=${CMAKE_SYSTEM_NAME}"
        "-DTARGET_ARCH=${keystitch_arch}"
        "-DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
        "-DCOMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}"
        "-DMINIMUM_MACOS=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        -P "${CMAKE_SOURCE_DIR}/cmake/WriteBuildInfo.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/BuildInfoGenerated.h"
    VERBATIM
)
add_dependencies(common refresh_build_info)
target_include_directories(common PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(common PRIVATE KEYSTITCH_BUILD_CONFIGURATION="$<CONFIG>")

if (HAVE_GETPWUID_R)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_GETPWUID_R=1")
endif()

