cmake_minimum_required(VERSION 3.15)

project(Idler
    VERSION 1.0.4
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

# The FFGL plugins, the FFGL SDK submodule and GLEW are all one dependency
# group, and the OpenFX plugin belongs to none of it -- its bundle links libc++
# and libSystem and nothing else. Turning this OFF builds the OFX plugin alone,
# with no GL loader anywhere in the configure.
#
# That is not a convenience: on Windows, GLEW arrives through vcpkg, so without
# it `find_package(GLEW REQUIRED)` fails at CONFIGURE time -- and a machine that
# only wants the OFX plugin would otherwise have to install a GL loader to build
# something that does not use one. Default ON, so CI is unchanged.
option(IDLER_BUILD_FFGL "Build the FFGL plugins (needs the FFGL SDK and, off macOS, GLEW)" ON)

# ---------------------------------------------------------------------------
# The Resolume FFGL SDK, as a git submodule pinned to b1afaf9 -- the rev the
# rest of the fleet pins. See AGENTS.md for what is wrong with it.
# ---------------------------------------------------------------------------
if(IDLER_BUILD_FFGL)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl/CMakeLists.txt")
        message(FATAL_ERROR
            "FFGL SDK not found. Run: git submodule update --init --recursive")
    endif()

    set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/ffgl EXCLUDE_FROM_ALL)
endif()

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
# The savers themselves. Each is a table row in Savers.cpp plus one file.
#
# Declared once because BOTH builds compile them -- the FFGL plugin and the
# OpenFX one. A duplicated list would let a twelfth saver be added to one and
# not the other, which builds and links perfectly on the side it was added to.
set(IDLER_SAVER_SOURCES
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

if(IDLER_BUILD_FFGL)

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

    ${IDLER_SAVER_SOURCES}
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
    # Raster.cpp is compiled into the harness as well as into the OFX bundle,
    # so `idtest --raster` measures the same rasteriser Resolve will run rather
    # than a copy of it. That is the whole point of the check.
    add_executable(idtest tools/idtest/main.cpp source/ofx/Raster.cpp)
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

endif()  # IDLER_BUILD_FFGL
#---------------------------------------------------------------------------
# OpenFX: the same eleven savers, rasterised in software.
#
# An OFX host hands over a buffer and most never offer a GL context, so this
# build swaps the renderer and reuses everything else -- Savers.cpp, Controls.cpp
# and Presets.h are shared source, not copies. `idtest --raster` is what keeps
# the two renderers agreeing.
#
# Both plugins live in ONE bundle: unlike FFGL, OFX resolves several plugins per
# binary, so the generator and the mask filter are two factories in one .ofx.
#---------------------------------------------------------------------------
option(BUILD_OFX "Build the OpenFX plugin" ON)

if(BUILD_OFX)
    set(OFX_SDK "${CMAKE_CURRENT_SOURCE_DIR}/external/openfx")

    add_library(IdlerOFX MODULE
        source/ofx/IdlerOFX.cpp
        source/ofx/Raster.cpp
        source/Controls.cpp
        source/Mesh.cpp
        source/Savers.cpp
        # The savers warn through Diag when a replay runs away, and that is
        # worth having in Resolve too: it is the one failure whose only other
        # symptom is the plugin taking a very long time over one frame.
        source/Diag.cpp
        ${IDLER_SAVER_SOURCES}
        ${OFX_SDK}/Support/Library/ofxsCore.cpp
        ${OFX_SDK}/Support/Library/ofxsImageEffect.cpp
        ${OFX_SDK}/Support/Library/ofxsInteract.cpp
        ${OFX_SDK}/Support/Library/ofxsLog.cpp
        ${OFX_SDK}/Support/Library/ofxsMultiThread.cpp
        ${OFX_SDK}/Support/Library/ofxsParams.cpp
        ${OFX_SDK}/Support/Library/ofxsProperty.cpp
        ${OFX_SDK}/Support/Library/ofxsPropertyValidation.cpp
    )
    target_include_directories(IdlerOFX PRIVATE
        source
        "${OFX_SDK}/include"
        "${OFX_SDK}/Support/include"
    )
    target_compile_definitions(IdlerOFX PRIVATE
        IDLER_VERSION="${PROJECT_VERSION}"
        PLUGIN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        PLUGIN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    )

    # @PROJECT_NAME@ and @OFX_BUNDLE_IDENTIFIER@ are what cmake/InfoOFX.plist.in
    # expects. Hardcoding a name there builds, loads and renders perfectly and
    # then fails codesign at release time -- see the comment in that file.
    set(OFX_BUNDLE_IDENTIFIER "com.stoatworks.idler")
    set(OFX_BUNDLE_DIR "${CMAKE_BINARY_DIR}/Idler.ofx.bundle")
    set_target_properties(IdlerOFX PROPERTIES
        PREFIX ""
        SUFFIX ".ofx"
        OUTPUT_NAME "${PROJECT_NAME}"
        LIBRARY_OUTPUT_DIRECTORY "${OFX_BUNDLE_DIR}/Contents/MacOS"
    )
    if(APPLE)
        configure_file(cmake/InfoOFX.plist.in
            "${OFX_BUNDLE_DIR}/Contents/Info.plist" @ONLY)
    elseif(WIN32)
        set_target_properties(IdlerOFX PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${OFX_BUNDLE_DIR}/Contents/Win64"
            RUNTIME_OUTPUT_DIRECTORY "${OFX_BUNDLE_DIR}/Contents/Win64"
            LIBRARY_OUTPUT_DIRECTORY_RELEASE "${OFX_BUNDLE_DIR}/Contents/Win64"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OFX_BUNDLE_DIR}/Contents/Win64"
            ARCHIVE_OUTPUT_DIRECTORY "${OFX_BUNDLE_DIR}/importlib"
        )
    endif()
endif()

