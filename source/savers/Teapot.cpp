#include "Teapot.h"

#include <algorithm>
#include <vector>

namespace idler
{
namespace
{
/// Segments around the axis of revolution. Low, because this is drawn at the
/// size of a pipe elbow and there can be several dozen of them in the box.
constexpr int kAround = 12;

/**
    The body profile, as (radius, height) pairs from the base upwards.

    Normalised so the tallest point is 1.0 and the widest is about 0.75, then
    scaled by the caller. The shape is the recognisable one: a nearly flat
    base, a belly a little below the middle, a waist, and a rim that flares
    back out.
*/
const Vec2 kProfile[] = {
	{ 0.00f, 0.02f },
	{ 0.26f, 0.00f },
	{ 0.44f, 0.04f },
	{ 0.60f, 0.14f },
	{ 0.72f, 0.30f },
	{ 0.75f, 0.44f },
	{ 0.70f, 0.56f },
	{ 0.60f, 0.64f },
	{ 0.52f, 0.68f },
	{ 0.56f, 0.70f },// the rim, flaring back out
	{ 0.54f, 0.72f }
};

/// The lid: a shallow dome and a knob.
const Vec2 kLidProfile[] = {
	{ 0.54f, 0.72f },
	{ 0.48f, 0.78f },
	{ 0.34f, 0.83f },
	{ 0.16f, 0.85f },
	{ 0.07f, 0.86f },
	{ 0.07f, 0.92f },
	{ 0.15f, 0.97f },
	{ 0.09f, 1.00f },
	{ 0.00f, 1.00f }
};

/// Revolve a profile about the Y axis.
///
/// Normals come from `SmoothNormals` afterwards rather than from the profile's
/// tangent, because the profile has a deliberate crease at the rim and
/// accumulating the face normals reproduces that crease correctly while an
/// analytic normal from a straight-line segment would round it off.
void Revolve( Mesh& mesh, const Mat4& transform, const Vec2* profile, int count,
              float scale, const Vec4& colour )
{
	const uint32_t base = mesh.Mark();

	for( int i = 0; i < count; ++i )
		for( int a = 0; a <= kAround; ++a )
		{
			const float angle = kTwoPi * static_cast< float >( a % kAround ) / static_cast< float >( kAround );
			const Vec3 local{ profile[ i ].x * std::cos( angle ) * scale,
			                  profile[ i ].y * scale,
			                  profile[ i ].x * std::sin( angle ) * scale };
			mesh.AddVertex( ( transform * Vec4( local, 1.0f ) ).xyz(), { 0.0f, 1.0f, 0.0f }, colour );
		}

	const uint32_t stride = static_cast< uint32_t >( kAround ) + 1;
	for( int i = 0; i + 1 < count; ++i )
		for( int a = 0; a < kAround; ++a )
		{
			const uint32_t v = base + static_cast< uint32_t >( i ) * stride + static_cast< uint32_t >( a );
			mesh.AddQuad( v, v + stride, v + stride + 1, v + 1 );
		}
}

/// A tube swept along a curve, tapering from `startRadius` to `endRadius`.
/// Used for the spout and the handle.
void Sweep( Mesh& mesh, const Mat4& transform, const std::vector< Vec3 >& path,
            float startRadius, float endRadius, const Vec4& colour )
{
	if( path.size() < 2 )
		return;

	const int sides     = 8;
	const uint32_t base = mesh.Mark();

	for( size_t i = 0; i < path.size(); ++i )
	{
		// The direction at this point: the average of the segments either side,
		// so the tube does not kink at a joint.
		const Vec3 forward = Normalise(
			( i == 0 ) ? path[ 1 ] - path[ 0 ]
			           : ( i + 1 == path.size() ) ? path[ i ] - path[ i - 1 ]
			                                      : path[ i + 1 ] - path[ i - 1 ] );

		const Mat4 frame = Mat4::Translate( path[ i ] ) * Mat4::AlignZTo( forward );

		const float t      = static_cast< float >( i ) / static_cast< float >( path.size() - 1 );
		const float radius = startRadius + ( endRadius - startRadius ) * t;

		for( int a = 0; a <= sides; ++a )
		{
			const float angle = kTwoPi * static_cast< float >( a % sides ) / static_cast< float >( sides );
			const Vec4 local{ std::cos( angle ) * radius, std::sin( angle ) * radius, 0.0f, 1.0f };
			const Vec4 world = transform * ( frame * local );
			mesh.AddVertex( world.xyz(), { 0.0f, 1.0f, 0.0f }, colour );
		}
	}

	const uint32_t stride = static_cast< uint32_t >( sides ) + 1;
	for( size_t i = 0; i + 1 < path.size(); ++i )
		for( int a = 0; a < sides; ++a )
		{
			const uint32_t v = base + static_cast< uint32_t >( i ) * stride + static_cast< uint32_t >( a );
			mesh.AddQuad( v, v + stride, v + stride + 1, v + 1 );
		}
}
} // namespace

void AddTeapot( Mesh& mesh, const Mat4& transform, float scale, const Vec4& colour )
{
	// The profile is 1.0 tall and 0.75 wide plus a spout that reaches further,
	// so this keeps the whole thing inside `scale` of the centre and puts the
	// centre of mass on the origin -- which is what lets Pipes drop one in
	// where a ball would have gone.
	const float unit = scale * 0.9f;
	const Mat4 place = transform * Mat4::Translate( { 0.0f, -unit * 0.45f, 0.0f } );

	const uint32_t start = mesh.Mark();

	Revolve( mesh, place, kProfile, static_cast< int >( sizeof( kProfile ) / sizeof( kProfile[ 0 ] ) ),
	         unit, colour );
	Revolve( mesh, place, kLidProfile, static_cast< int >( sizeof( kLidProfile ) / sizeof( kLidProfile[ 0 ] ) ),
	         unit, colour );

	// The spout: out of the belly, rising above the rim, narrowing.
	std::vector< Vec3 > spout = {
		{ 0.55f * unit, 0.30f * unit, 0.0f },
		{ 0.80f * unit, 0.36f * unit, 0.0f },
		{ 1.00f * unit, 0.52f * unit, 0.0f },
		{ 1.08f * unit, 0.72f * unit, 0.0f },
		{ 1.06f * unit, 0.82f * unit, 0.0f }
	};
	Sweep( mesh, place, spout, 0.17f * unit, 0.07f * unit, colour );

	// The handle: an arc out of the other side and back.
	std::vector< Vec3 > handle;
	for( int i = 0; i <= 8; ++i )
	{
		const float t     = static_cast< float >( i ) / 8.0f;
		const float angle = kPi * ( 0.15f + t * 0.7f );
		handle.push_back( { -( 0.48f + 0.36f * std::sin( angle ) ) * unit,
		                    ( 0.30f + 0.38f * ( 1.0f - std::cos( angle ) ) ) * unit,
		                    0.0f } );
	}
	Sweep( mesh, place, handle, 0.09f * unit, 0.09f * unit, colour );

	// One pass over everything just added. Vertices at the same position get
	// the same normal, so the body and the lid meet at the rim without a seam,
	// while the deliberate crease in the profile survives because the faces
	// either side of it really do point in different directions.
	mesh.SmoothNormals( start );
}

} // namespace idler
