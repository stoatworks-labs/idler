#pragma once

#include <cstdint>
#include <vector>

#include "Vec.h"

/**
    What a saver hands back, and the only thing the renderer knows how to draw.

    Eleven savers, one draw call. A saver does not touch OpenGL: it fills a
    `Scene` -- a camera, some lights, and one triangle mesh -- and the renderer
    in Idler.cpp uploads that and draws it. Nothing else.

    Three things follow from that, and they are the reason it is shaped this
    way:

    - **A saver is testable without a GPU.** `idtest --geometry` builds a scene
      and measures the mesh: how many triangles, where its bounding box is,
      whether the pipe network is connected. Those are the assertions that
      catch a broken saver, and none of them needs a context.
    - **Adding a saver cannot break the others.** There is no shared GL state to
      leave dirty, because a saver never sets any.
    - **The OFX build renders the same scenes.** Resolve gets a software
      rasteriser over this exact structure rather than a second implementation
      of eleven screensavers.

    ## Everything is triangles, including the lines

    Mystify, Beziers and Curves and Colors are line drawings, and the obvious
    way to draw them is `GL_LINES` with `glLineWidth`. That does not work.
    **A core profile is only required to support a line width of 1.0**, and on
    macOS that is exactly what it supports -- `glLineWidth( 4 )` raises no
    error, sets no state, and draws hairlines. Since the whole look of Mystify
    is a fat ribbon of trailing polygons, the line width has to be real
    geometry, so `Mesh::AddLine` expands a segment into a quad with mitred
    joins. The renderer never issues a `GL_LINES` draw.
*/
namespace idler
{

/// Position, normal, colour. One interleaved buffer.
///
/// Colour is per-vertex rather than per-draw because most of these savers
/// colour things individually -- every star its own brightness, every pipe
/// segment its own hue, every Mystify trail step its own point in the cycle --
/// and per-vertex colour turns all of that into one draw call.
struct Vertex
{
	Vec3 position;
	Vec3 normal;
	Vec4 colour;

	Vertex() = default;
	Vertex( const Vec3& p, const Vec3& n, const Vec4& c ) : position( p ), normal( n ), colour( c ) {}
};

/**
    An indexed triangle mesh, plus the builders the savers actually use.

    Grown in place and cleared between frames rather than reallocated, because
    a saver runs sixty times a second inside a host that is already allocating
    as hard as it can.
*/
struct Mesh
{
	std::vector< Vertex > vertices;
	std::vector< uint32_t > indices;

	void Clear()
	{
		vertices.clear();
		indices.clear();
	}

	bool Empty() const { return indices.empty(); }

	size_t TriangleCount() const { return indices.size() / 3; }

	/// Index of the next vertex to be added. Callers use this as the base for
	/// their own index arithmetic.
	uint32_t Mark() const { return static_cast< uint32_t >( vertices.size() ); }

	void AddVertex( const Vec3& p, const Vec3& n, const Vec4& c ) { vertices.emplace_back( p, n, c ); }

	void AddTriangle( uint32_t a, uint32_t b, uint32_t c )
	{
		indices.push_back( a );
		indices.push_back( b );
		indices.push_back( c );
	}

	/// Two triangles over four already-added vertices, wound a-b-c-d.
	void AddQuad( uint32_t a, uint32_t b, uint32_t c, uint32_t d )
	{
		AddTriangle( a, b, c );
		AddTriangle( a, c, d );
	}

	//-----------------------------------------------------------------------
	// Flat builders, for the 2D savers. Z is written but the depth test is off;
	// draw order is index order.
	//-----------------------------------------------------------------------

	/// A thick line segment in the XY plane, as a quad.
	///
	/// `width` is the full width, so the quad reaches half of it either side of
	/// the centre line -- which is what makes a two-pixel line look two pixels
	/// wide rather than four.
	void AddLine( const Vec2& a, const Vec2& b, float width, const Vec4& colourA, const Vec4& colourB );

	/// A polyline with mitred joins.
	///
	/// Mitred rather than each segment drawn on its own, because separate quads
	/// leave a wedge-shaped notch on the outside of every corner. On a Mystify
	/// polygon -- which is nothing but corners -- that reads as the line being
	/// dashed. The mitre is limited to four times the width; past that the
	/// corner is so sharp that an exact mitre would shoot a spike off across
	/// the frame, so it falls back to a bevel.
	void AddPolyline( const Vec2* points, int count, bool closed, float width,
	                  const Vec4* colours );

	/// A filled axis-aligned rectangle.
	void AddRect( const Vec2& min, const Vec2& max, const Vec4& colour );

	//-----------------------------------------------------------------------
	// Solid builders, for the 3D savers.
	//-----------------------------------------------------------------------

	/// An axis-aligned box, flat shaded, centred on `centre`.
	void AddBox( const Vec3& centre, const Vec3& halfExtent, const Vec4& colour );

	/// A box transformed by an arbitrary matrix. Normals go through the same
	/// matrix, which is correct only because nothing here scales non-uniformly
	/// -- and a non-uniform scale would need the inverse transpose. Asserted by
	/// `idtest --geometry`, which checks that every emitted normal is unit
	/// length.
	void AddTransformedBox( const Mat4& transform, const Vec3& halfExtent, const Vec4& colour );

	/// A cylinder along +Z from z=0 to z=length, radius `radius`, `sides`
	/// around. Smooth-shaded around the barrel, flat on the caps.
	void AddCylinder( const Mat4& transform, float radius, float length, int sides,
	                  const Vec4& colour, bool capStart, bool capEnd );

	/// A UV sphere. `rings` by `segments`, smooth shaded.
	void AddSphere( const Mat4& transform, float radius, int rings, int segments, const Vec4& colour );

	/// A torus in the XY plane, smooth shaded.
	void AddTorus( const Mat4& transform, float majorRadius, float minorRadius,
	               int majorSegments, int minorSegments, const Vec4& colour );

	/// Recompute every normal by area-weighted accumulation over the faces that
	/// share each position. Used by the savers that build a surface a triangle
	/// at a time and want it smooth (FlowerBox's morph, the teapot's patches).
	void SmoothNormals( uint32_t fromVertex );

	/// Append `other`, transformed. Used where a saver builds a part once and
	/// stamps it -- every 3D Text glyph, every Flying Object.
	void Append( const Mesh& other, const Mat4& transform );
};

/// How the mesh is shaded.
enum class Shading
{
	Flat = 0,   ///< The vertex colour, unlit. What the 2D savers want.
	Lit,        ///< Diffuse plus ambient from one directional light.
	Wireframe,  ///< Lit, but only the triangle edges. Built as geometry, not
	            ///< `glPolygonMode` -- see the note on line width in Mesh.

	Count
};

/**
    One frame's worth of everything the renderer needs.

    Rebuilt from scratch every frame. There is no "scene graph" and nothing
    persists here between frames -- the growing savers keep their state in
    their own objects, and what lands in a `Scene` is always the finished
    picture for one instant.
*/
struct Scene
{
	Mat4 view = Mat4::Identity();
	Mat4 proj = Mat4::Identity();

	Mesh mesh;

	/// Off for the 2D savers, which are painter's-algorithm drawings, and on
	/// for the 3D ones. Both go through the same shader and the same buffer;
	/// this is the only thing that differs.
	bool depthTest = false;

	Shading shading = Shading::Flat;

	/// Direction the light travels, in view space -- so the lighting does not
	/// swing round as a saver's camera orbits, which is what a light fixed in
	/// world space would do and which reads as the object being lit by a
	/// searchlight rather than by the room.
	Vec3 lightDirection = Normalise( Vec3( -0.35f, -0.6f, -0.72f ) );
	float ambient       = 0.28f;

	/// Premultiplied by the caller. The renderer clears to this.
	Vec4 background = { 0.0f, 0.0f, 0.0f, 1.0f };

	/// Fog, as a fraction of the far plane at which geometry has fully faded.
	/// 0 disables it. 3D Maze and 3D Pipes both want it -- a corridor that ends
	/// in a hard-edged wall at the far clip reads as a bug, and the original
	/// savers faded to black there too.
	float fogStart = 0.0f;
	float fogEnd   = 0.0f;

	void Clear()
	{
		mesh.Clear();
		view      = Mat4::Identity();
		proj      = Mat4::Identity();
		depthTest = false;
		shading   = Shading::Flat;
		fogStart  = 0.0f;
		fogEnd    = 0.0f;
	}
};

} // namespace idler
