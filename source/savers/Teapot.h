#pragma once

#include "../Scene.h"

/**
    A teapot, for 3D Pipes to use as a joint.

    ## It is not the Utah teapot

    The original screensaver used Newell's teapot -- the 32 bicubic patches
    every graphics course ships with. This one is built from a profile curve, a
    swept spout and a torus arc, and it is a teapot rather than *the* teapot.

    That is a deliberate choice and worth stating, because the obvious move is
    to paste in the 306 control points. Two reasons not to:

    - **It is 3D Pipes' joint, at the size of a pipe elbow.** At that scale the
      difference between Newell's silhouette and a good profile curve is
      somewhere below one pixel. The patch data would be several hundred
      numbers of table for something nobody can see.
    - **A table of numbers copied from memory is a table of numbers that can be
      quietly wrong.** A generated teapot either has a spout or it does not,
      and `idtest --geometry` can check its bounding box and that its surface
      is closed. There is no equivalent check for "is control point 214
      correct".

    What it keeps is what makes a teapot read as one at a glance: the squat
    body, the flared lid with a knob, a spout that rises above the rim, and a
    handle on the other side.

    Built along +Y, centred on the origin, and scaled so the whole thing fits
    within `scale` of the centre -- so a caller can treat it as a sphere of
    that radius, which is exactly how Pipes places it.
*/
namespace idler
{

void AddTeapot( Mesh& mesh, const Mat4& transform, float scale, const Vec4& colour );

} // namespace idler
