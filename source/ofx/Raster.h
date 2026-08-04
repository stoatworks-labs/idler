#pragma once

#include <vector>

#include "../Scene.h"

/**
    A software rasteriser over `Scene`, for the OpenFX build.

    ## Why this exists rather than a GL context

    An OFX host hands a plugin a buffer and expects pixels back. Some hosts
    offer an OpenGL render path, most do not, and none of them guarantees one --
    Resolve will call the CPU path whenever it feels like it, and Natron has no
    GL path at all. A plugin that only knows how to draw through GL is a plugin
    that does not render.

    The alternative to this file is a second implementation of eleven
    screensavers, which is the thing `Scene` was shaped to avoid: a saver fills
    a camera, a light and one triangle mesh, and knows nothing about how it is
    drawn. So the OFX build reuses `Savers.cpp` unchanged and swaps only the
    thing at the bottom.

    ## It must agree with the shader, not merely look similar

    Everything here mirrors `Shaders.cpp`, and where it does the same arithmetic
    it does it in the same order. That is not tidiness: the two builds are sold
    as the same plugin, and a preset that looks right in Resolume and wrong in
    Resolve is worse than one that is wrong in both. `idtest --raster` renders
    every saver through both paths and compares, which is the only thing that
    keeps them honest.

    The GL state being reproduced (from `Idler.cpp`):

      - clear to `scene.background`, depth to 1.0
      - `GL_DEPTH_TEST` with `GL_LESS`, only when `scene.depthTest`
      - `GL_CULL_FACE` **off** -- the shading is two-sided on purpose
      - `glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA )`, premultiplied over
      - triangles drawn in index order, never sorted

    The blend and the depth test interact, which is why draw order is preserved
    exactly: with both on, the picture depends on the order the mesh was built
    in, and a rasteriser that sorted for efficiency would draw a different one.
*/
namespace idler
{

/**
    Draw `scene` into a premultiplied RGBA float buffer, top row first.

    `rgba` must hold `width * height * 4` floats. `edgeWidth` is the wireframe
    line half-width in pixels, the same number the GL path passes as its
    `EdgeWidth` uniform, so the caller derives it the same way for both.
*/
void Rasterise( const Scene& scene, float* rgba, int width, int height, float edgeWidth );

}
