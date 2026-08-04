#pragma once

/**
    The two shader programs, and the vertex layout they agree on.

    There are only two, and that is the point. Eleven savers, one way of
    drawing: a saver fills a `Scene`, the scene program draws that mesh into the
    off-screen target, and the composite program puts the target on the output --
    over the clip, into it, or on its own.

    ## The interleaved vertex

    Thirteen floats: position (3), normal (3), colour (4), barycentric (3).

    The barycentric is not part of `Vertex` in Scene.h, because no saver sets it.
    It is added by the renderer while it packs the upload buffer, and it exists
    for the wireframe shading mode. Wireframe is drawn by fading in the fragments
    near a triangle edge, which needs each fragment to know how far it is from
    one -- and the only way to know that without a geometry shader is to give
    each of a triangle's three vertices a different corner of (1,0,0), (0,1,0),
    (0,0,1) and let the interpolator do it.

    That means the mesh has to be **de-indexed** for wireframe, since a shared
    vertex cannot carry three different barycentrics at once. The renderer does
    that only when wireframe is on. In every other mode the barycentric is
    (1,1,1) and the fragment shader ignores it.

    Why not `glPolygonMode( GL_LINE )`: for the same reason `Mesh` builds its 2D
    lines out of quads. A core profile is only required to support a line width
    of 1.0, which is what macOS supports, so a wireframe drawn that way is
    hairline-thin at every resolution and disappears entirely on a 4K output.

    ## Premultiplied alpha, throughout

    The scene program writes premultiplied RGBA and the target is blended with
    `GL_ONE, GL_ONE_MINUS_SRC_ALPHA`. That is what makes the fog correct: fog
    fades a fragment toward *nothing* rather than toward black, so a maze
    corridor receding into the distance composites correctly over whatever is
    behind the plugin, instead of fading into a black rectangle the shape of the
    frame. Straight alpha cannot express that without a second pass.
*/
namespace idler
{

/// Floats per vertex in the upload buffer.
constexpr int kFloatsPerVertex = 13;

/// Attribute locations, matching the `layout( location = )` in the shader.
enum VertexAttribute
{
	kAttrPosition    = 0,
	kAttrNormal      = 1,
	kAttrColour      = 2,
	kAttrBarycentric = 3
};

/// Draws a Scene's mesh into the off-screen target.
const char* SceneVertexShader();
const char* SceneFragmentShader();

/// Puts the target on the output. Compiled twice: once with `HAS_INPUT`
/// defined, for the effect, and once without, for the source.
///
/// A `#define` rather than a uniform branch because the source has no input
/// texture to bind at all, and sampling an unbound sampler2D is undefined
/// rather than merely wasteful -- on some drivers it reads the last texture
/// anything bound to that unit, which in a host as busy as Resolume is another
/// layer's clip.
const char* CompositeVertexShader();
const char* CompositeFragmentShader( bool hasInput );

} // namespace idler
