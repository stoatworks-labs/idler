cmake_minimum_required(VERSION 3.15)

project(Idler
    VERSION 0.1.0
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Universal by default so one bundle loads in both the Apple Silicon and the
# Intel build of Resolume. Override with -DCMAKE_OSX_ARCHITECTURES=arm64 for a
# faster dev build.
#
# This must sit immediately after project() and before any target exists. CMake
# latches the architecture list when the first target is created; setting it
# later is silently ignored and you get an arm64-only binary that the build log
# calls a success. Verify with `lipo`, never with the log.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple deprecated the whole OpenGL API in 10.14 and FFGL's plugin interface is
# still OpenGL, so every SDK call warns. There is no alternative API to move to
# here, so silencing it is not ignoring a problem.
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

# windows.h defines min and max as macros, which turns every std::min into
# `std::(...)` and fails with an error naming neither min nor windows.h. Set
# globally rather than per-file: the next file to include windows.h hits it too,
# and the error gives no hint what happened.
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

option(IDLER_BUILD_TOOLS "Build the offline render and measurement harness" ON)

# ---------------------------------------------------------------------------
# The Resolume FFGL SDK, as a git submodule pinned to b1afaf9 -- the rev the
# rest of the fleet pins. See AGENTS.md for what is wrong with it.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl/CMakeLists.txt")
    message(FATAL_ERROR
        "FFGL SDK not found. Run: git submodule update --init --recursive")
endif()

set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/ffgl EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Everything both plugins share, as an OBJECT library.
#
# OBJECT and not STATIC on purpose. `CFFGLPluginInfo` registers itself from a
# file-scope constructor and nothing ever references that object by name, so in
# an archive the linker is entitled to drop the whole translation unit -- and
# the result is a bundle that loads, exports plugMain, and reports that it
# contains no plugins.
#
# Note what is NOT in here: SourcePlugin.cpp and EffectPlugin.cpp. Each carries
# its own CFFGLPluginInfo with its own plugin ID, and each is listed directly in
# its own MODULE target below. Putting either in the shared library would
# register both plugins into both bundles.
# ---------------------------------------------------------------------------
add_library(idler_core OBJECT
    source/Diag.h
    source/Diag.cpp
    source/Hash.h
    source/Vec.h
    source/Scene.h
    source/Mesh.cpp
    source/Savers.h
    source/Savers.cpp
    source/Controls.h
    source/Controls.cpp
    source/Presets.h
    source/Shaders.h
    source/Shaders.cpp
    source/Target.h
    source/Target.cpp
    source/Idler.h
    source/Idler.cpp

    # The savers themselves. Each is a table row in Savers.cpp plus one file.
    source/savers/Mystify.cpp
    source/savers/Beziers.cpp
    source/savers/Curves.cpp
    source/savers/FlyingWindows.cpp
    source/savers/Starfield.cpp
    source/savers/Marquee.cpp
    source/savers/Maze.cpp
    source/savers/Pipes.cpp
    source/savers/FlyingObjects.cpp
    source/savers/FlowerBox.cpp
    source/savers/Text3D.cpp
    source/savers/Font.h
    source/savers/Font.cpp
    source/savers/Teapot.h
    source/savers/Teapot.cpp
)

target_include_directories(idler_core PUBLIC source)
target_link_libraries(idler_core PUBLIC ffgl::sdk)
target_compile_definitions(idler_core PUBLIC IDLER_VERSION="${PROJECT_VERSION}")
set_target_properties(idler_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# On Windows/Linux the SDK headers we include pull in <GL/glew.h>. The SDK links
# GLEW privately, so consumers need it too -- for the include path, and for the
# GLEW_STATIC define the static vcpkg triplet propagates. macOS uses the system
# OpenGL framework and needs none of this.
#
# GLEW arrives through vcpkg.json, which nothing in this file mentions. Delete
# that manifest and every macOS build still passes while Windows fails at
# CONFIGURE time, pointing at this line.
if(NOT APPLE)
    find_package(GLEW REQUIRED)
    target_link_libraries(idler_core PUBLIC GLEW::GLEW)
endif()

# ---------------------------------------------------------------------------
# The two plugins. FFGL resolves one plugMain per binary, so a source and an
# effect are two bundles -- not one bundle with two entries.
# ---------------------------------------------------------------------------
function(idler_add_plugin target entry bundle_name identifier)
    add_library(${target} MODULE ${entry})
    target_link_libraries(${target} PRIVATE idler_core)

    if(APPLE)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "bundle"
            OUTPUT_NAME "${bundle_name}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${bundle_name}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${identifier}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        )
        # FFGL hosts resolve plugMain by symbol name, so keep default visibility.
        target_link_libraries(${target} PRIVATE
            "-framework OpenGL"
            "-framework Carbon"
            "-framework AppKit"
        )
    else()
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${bundle_name}")
    endif()
endfunction()

idler_add_plugin(IdlerSource source/SourcePlugin.cpp
    "Idler" "com.stoatworks.ffgl.idler")
idler_add_plugin(IdlerEffect source/EffectPlugin.cpp
    "Idler Mask" "com.stoatworks.ffgl.idler.mask")

# ---------------------------------------------------------------------------
# Offline harness. Where the savers are actually verified rather than merely
# previewed: it drives the real plugin class through the real FFGL sequence in a
# headless core-profile context. See tools/idtest.
# ---------------------------------------------------------------------------
if(IDLER_BUILD_TOOLS AND APPLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/idtest/main.cpp")
    add_executable(idtest tools/idtest/main.cpp)
    # zlib ships with the OS, which is why the PNG writer is fifty lines here
    # rather than a vendored dependency.
    target_link_libraries(idtest PRIVATE idler_core z
        "-framework OpenGL"
        "-framework AppKit"
    )
endif()

# ---------------------------------------------------------------------------
# `cmake --install` drops both bundles straight into the user's Resolume plugin
# folder. Override with -DCMAKE_INSTALL_PREFIX=...
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # honour an explicit prefix
elseif(APPLE)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Documents/Resolume Arena/Extra Effects"
        CACHE PATH "" FORCE)
endif()

install(TARGETS IdlerSource IdlerEffect
    LIBRARY DESTINATION "."
    BUNDLE  DESTINATION "."
)
