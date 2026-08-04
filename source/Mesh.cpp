#include "Scene.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace idler
{
namespace
{
/// The 2D builders work in the XY plane and are never lit, so they all share
/// one normal. Kept as a named constant rather than written out at each call
/// site so that a later change -- lighting the 2D savers, say -- has one place
/// to happen.
const Vec3 kFlatNormal{ 0.0f, 0.0f, 1.0f };

/// Perpendicular to a 2D direction, left-hand side.
Vec2 Perp( const Vec2& d )
{
	return { -d.y, d.x };
}

Vec2 NormaliseSafe( const Vec2& v )
{
	const float len = std::sqrt( v.x * v.x + v.y * v.y );
	if( len < 1e-9f )
		return { 1.0f, 0.0f };
	return { v.x / len, v.y / len };
}
} // namespace

void Mesh::AddLine( const Vec2& a, const Vec2& b, float width, const Vec4& colourA, const Vec4& colourB )
{
	const Vec2 dir = NormaliseSafe( b - a );
	const Vec2 n   = Perp( dir ) * ( width * 0.5f );

	const uint32_t base = Mark();
	AddVertex( { a.x - n.x, a.y - n.y, 0.0f }, kFlatNormal, colourA );
	AddVertex( { a.x + n.x, a.y + n.y, 0.0f }, kFlatNormal, colourA );
	AddVertex( { b.x + n.x, b.y + n.y, 0.0f }, kFlatNormal, colourB );
	AddVertex( { b.x - n.x, b.y - n.y, 0.0f }, kFlatNormal, colourB );
	AddQuad( base, base + 1, base + 2, base + 3 );
}

void Mesh::AddPolyline( const Vec2* points, int count, bool closed, float width, const Vec4* colours )
{
	if( count < 2 )
		return;

	const float half = width * 0.5f;
	// A mitre grows without bound as the corner closes up; past this the spike
	// would shoot off across the frame, so the join falls back to the segment
	// normal, which is a bevel.
	const float kMitreLimit = 4.0f;

	const int segmentCount = closed ? count : count - 1;
	const uint32_t base    = Mark();

	// One pair of vertices per point (two extra for an open line's ends is not
	// needed -- the end normals are just the end segments' normals).
	for( int i = 0; i < count; ++i )
	{
		const Vec2& here = points[ i ];

		// The directions of the segments meeting at this point. For an open
		// polyline the ends have only one, so it is used for both sides.
		const bool hasPrev = closed || i > 0;
		const bool hasNext = closed || i < count - 1;

		const Vec2 prevDir = hasPrev
		                       ? NormaliseSafe( here - points[ ( i - 1 + count ) % count ] )
		                       : NormaliseSafe( points[ i + 1 ] - here );
		const Vec2 nextDir = hasNext
		                       ? NormaliseSafe( points[ ( i + 1 ) % count ] - here )
		                       : NormaliseSafe( here - points[ i - 1 ] );

		// The mitre direction bisects the two segment normals; its length is
		// 1/cos(theta/2), which is what makes the outer edge meet cleanly.
		const Vec2 nPrev = Perp( prevDir );
		const Vec2 nNext = Perp( nextDir );
		Vec2 mitre       = NormaliseSafe( nPrev + nNext );

		const float cosHalf = mitre.x * nPrev.x + mitre.y * nPrev.y;
		float scale         = ( std::fabs( cosHalf ) < 1e-4f ) ? kMitreLimit : 1.0f / cosHalf;
		if( scale > kMitreLimit || scale < -kMitreLimit )
		{
			mitre = nNext;
			scale = 1.0f;
		}

		const Vec2 offset = mitre * ( half * scale );
		const Vec4 colour = colours ? colours[ i ] : Vec4( 1.0f, 1.0f, 1.0f, 1.0f );

		AddVertex( { here.x - offset.x, here.y - offset.y, 0.0f }, kFlatNormal, colour );
		AddVertex( { here.x + offset.x, here.y + offset.y, 0.0f }, kFlatNormal, colour );
	}

	for( int s = 0; s < segmentCount; ++s )
	{
		const uint32_t i0 = base + static_cast< uint32_t >( s ) * 2;
		const uint32_t i1 = base + static_cast< uint32_t >( ( s + 1 ) % count ) * 2;
		AddQuad( i0, i0 + 1, i1 + 1, i1 );
	}
}

void Mesh::AddRect( const Vec2& min, const Vec2& max, const Vec4& colour )
{
	const uint32_t base = Mark();
	AddVertex( { min.x, min.y, 0.0f }, kFlatNormal, colour );
	AddVertex( { max.x, min.y, 0.0f }, kFlatNormal, colour );
	AddVertex( { max.x, max.y, 0.0f }, kFlatNormal, colour );
	AddVertex( { min.x, max.y, 0.0f }, kFlatNormal, colour );
	AddQuad( base, base + 1, base + 2, base + 3 );
}

void Mesh::AddBox( const Vec3& centre, const Vec3& halfExtent, const Vec4& colour )
{
	AddTransformedBox( Mat4::Translate( centre ), halfExtent, colour );
}

void Mesh::AddTransformedBox( const Mat4& transform, const Vec3& halfExtent, const Vec4& colour )
{
	// Flat shaded, so each face carries its own four vertices. Sharing the
	// eight corners would average the three face normals at every corner and
	// give a box that looks like a rounded die.
	static const Vec3 kFaceNormals[ 6 ] = {
		{ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
		{ 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }
	};
	// Counter-clockwise seen from outside, so back-face culling keeps the
	// outside.
	static const int kFaceCorners[ 6 ][ 4 ] = {
		{ 0, 1, 2, 3 }, { 5, 4, 7, 6 }, { 1, 5, 6, 2 },
		{ 4, 0, 3, 7 }, { 3, 2, 6, 7 }, { 4, 5, 1, 0 }
	};
	const Vec3 corner[ 8 ] = {
		{ -halfExtent.x, -halfExtent.y, halfExtent.z },
		{ halfExtent.x, -halfExtent.y, halfExtent.z },
		{ halfExtent.x, halfExtent.y, halfExtent.z },
		{ -halfExtent.x, halfExtent.y, halfExtent.z },
		{ -halfExtent.x, -halfExtent.y, -halfExtent.z },
		{ halfExtent.x, -halfExtent.y, -halfExtent.z },
		{ halfExtent.x, halfExtent.y, -halfExtent.z },
		{ -halfExtent.x, halfExtent.y, -halfExtent.z }
	};

	for( int f = 0; f < 6; ++f )
	{
		const uint32_t base = Mark();
		const Vec3 n        = Normalise( transform.TransformDirection( kFaceNormals[ f ] ) );
		for( int c = 0; c < 4; ++c )
		{
			const Vec4 p = transform * Vec4( corner[ kFaceCorners[ f ][ c ] ], 1.0f );
			AddVertex( p.xyz(), n, colour );
		}
		AddQuad( base, base + 1, base + 2, base + 3 );
	}
}

void Mesh::AddCylinder( const Mat4& transform, float radius, float length, int sides,
                        const Vec4& colour, bool capStart, bool capEnd )
{
	sides = std::max( 3, sides );

	const uint32_t ringBase = Mark();
	for( int i = 0; i <= sides; ++i )
	{
		// The seam vertex is duplicated (i == sides repeats i == 0) so that the
		// barrel could carry texture coordinates later without the last quad
		// wrapping the whole way back across the texture.
		const float a  = kTwoPi * static_cast< float >( i % sides ) / static_cast< float >( sides );
		const float ca = std::cos( a ), sa = std::sin( a );
		const Vec3 nLocal{ ca, sa, 0.0f };
		const Vec3 n = Normalise( transform.TransformDirection( nLocal ) );

		const Vec4 p0 = transform * Vec4( { ca * radius, sa * radius, 0.0f }, 1.0f );
		const Vec4 p1 = transform * Vec4( { ca * radius, sa * radius, length }, 1.0f );
		AddVertex( p0.xyz(), n, colour );
		AddVertex( p1.xyz(), n, colour );
	}

	for( int i = 0; i < sides; ++i )
	{
		const uint32_t a = ringBase + static_cast< uint32_t >( i ) * 2;
		AddQuad( a, a + 2, a + 3, a + 1 );
	}

	auto addCap = [ & ]( float z, bool facingPositiveZ ) {
		const Vec3 nLocal{ 0.0f, 0.0f, facingPositiveZ ? 1.0f : -1.0f };
		const Vec3 n        = Normalise( transform.TransformDirection( nLocal ) );
		const uint32_t base = Mark();

		const Vec4 centre = transform * Vec4( { 0.0f, 0.0f, z }, 1.0f );
		AddVertex( centre.xyz(), n, colour );
		for( int i = 0; i < sides; ++i )
		{
			const float a = kTwoPi * static_cast< float >( i ) / static_cast< float >( sides );
			const Vec4 p  = transform * Vec4( { std::cos( a ) * radius, std::sin( a ) * radius, z }, 1.0f );
			AddVertex( p.xyz(), n, colour );
		}
		for( int i = 0; i < sides; ++i )
		{
			const uint32_t a = base + 1 + static_cast< uint32_t >( i );
			const uint32_t b = base + 1 + static_cast< uint32_t >( ( i + 1 ) % sides );
			if( facingPositiveZ )
				AddTriangle( base, a, b );
			else
				AddTriangle( base, b, a );
		}
	};

	if( capStart )
		addCap( 0.0f, false );
	if( capEnd )
		addCap( length, true );
}

void Mesh::AddSphere( const Mat4& transform, float radius, int rings, int segments, const Vec4& colour )
{
	rings    = std::max( 2, rings );
	segments = std::max( 3, segments );

	const uint32_t base = Mark();
	for( int r = 0; r <= rings; ++r )
	{
		const float phi = kPi * static_cast< float >( r ) / static_cast< float >( rings );
		const float sp = std::sin( phi ), cp = std::cos( phi );
		for( int s = 0; s <= segments; ++s )
		{
			const float theta = kTwoPi * static_cast< float >( s % segments ) / static_cast< float >( segments );
			const Vec3 nLocal{ sp * std::cos( theta ), cp, sp * std::sin( theta ) };
			const Vec3 n  = Normalise( transform.TransformDirection( nLocal ) );
			const Vec4 p  = transform * Vec4( nLocal * radius, 1.0f );
			AddVertex( p.xyz(), n, colour );
		}
	}

	const uint32_t stride = static_cast< uint32_t >( segments ) + 1;
	for( int r = 0; r < rings; ++r )
		for( int s = 0; s < segments; ++s )
		{
			const uint32_t a = base + static_cast< uint32_t >( r ) * stride + static_cast< uint32_t >( s );
			const uint32_t b = a + stride;
			AddQuad( a, b, b + 1, a + 1 );
		}
}

void Mesh::AddTorus( const Mat4& transform, float majorRadius, float minorRadius,
                     int majorSegments, int minorSegments, const Vec4& colour )
{
	majorSegments = std::max( 3, majorSegments );
	minorSegments = std::max( 3, minorSegments );

	const uint32_t base = Mark();
	for( int i = 0; i <= majorSegments; ++i )
	{
		const float u  = kTwoPi * static_cast< float >( i % majorSegments ) / static_cast< float >( majorSegments );
		const float cu = std::cos( u ), su = std::sin( u );
		for( int j = 0; j <= minorSegments; ++j )
		{
			const float v  = kTwoPi * static_cast< float >( j % minorSegments ) / static_cast< float >( minorSegments );
			const float cv = std::cos( v ), sv = std::sin( v );

			const Vec3 nLocal{ cu * cv, su * cv, sv };
			const Vec3 pLocal{ cu * ( majorRadius + minorRadius * cv ),
			                   su * ( majorRadius + minorRadius * cv ),
			                   minorRadius * sv };

			AddVertex( ( transform * Vec4( pLocal, 1.0f ) ).xyz(),
			           Normalise( transform.TransformDirection( nLocal ) ), colour );
		}
	}

	const uint32_t stride = static_cast< uint32_t >( minorSegments ) + 1;
	for( int i = 0; i < majorSegments; ++i )
		for( int j = 0; j < minorSegments; ++j )
		{
			const uint32_t a = base + static_cast< uint32_t >( i ) * stride + static_cast< uint32_t >( j );
			const uint32_t b = a + stride;
			AddQuad( a, a + 1, b + 1, b );
		}
}

void Mesh::SmoothNormals( uint32_t fromVertex )
{
	// Accumulate the un-normalised cross product of each face into its three
	// vertices. Un-normalised on purpose: its magnitude is twice the triangle
	// area, so large faces pull harder than the slivers a morph throws off,
	// which is what stops a single degenerate triangle steering the shading of
	// a whole region.
	for( uint32_t i = fromVertex; i < vertices.size(); ++i )
		vertices[ i ].normal = { 0.0f, 0.0f, 0.0f };

	// Vertices at the same position must share a normal or the seam shows as a
	// crease. The savers that call this build their surfaces from separate
	// patches that meet exactly, so the key is the exact bit pattern -- these
	// are computed from the same expression on both sides, not merely close.
	struct PositionKey
	{
		float x, y, z;
		bool operator==( const PositionKey& o ) const { return x == o.x && y == o.y && z == o.z; }
	};
	struct PositionHash
	{
		size_t operator()( const PositionKey& k ) const
		{
			auto bits = []( float f ) {
				uint32_t u;
				std::memcpy( &u, &f, sizeof( u ) );
				return u;
			};
			return ( static_cast< size_t >( bits( k.x ) ) * 73856093U ) ^
			       ( static_cast< size_t >( bits( k.y ) ) * 19349663U ) ^
			       ( static_cast< size_t >( bits( k.z ) ) * 83492791U );
		}
	};

	std::unordered_map< PositionKey, Vec3, PositionHash > accumulated;
	accumulated.reserve( vertices.size() - fromVertex );

	for( size_t t = 0; t + 2 < indices.size(); t += 3 )
	{
		const uint32_t ia = indices[ t ], ib = indices[ t + 1 ], ic = indices[ t + 2 ];
		if( ia < fromVertex )
			continue;

		const Vec3& a = vertices[ ia ].position;
		const Vec3& b = vertices[ ib ].position;
		const Vec3& c = vertices[ ic ].position;
		const Vec3 faceNormal = Cross( b - a, c - a );

		for( uint32_t index : { ia, ib, ic } )
		{
			const PositionKey key{ vertices[ index ].position.x,
			                       vertices[ index ].position.y,
			                       vertices[ index ].position.z };
			accumulated[ key ] += faceNormal;
		}
	}

	for( uint32_t i = fromVertex; i < vertices.size(); ++i )
	{
		const PositionKey key{ vertices[ i ].position.x, vertices[ i ].position.y, vertices[ i ].position.z };
		vertices[ i ].normal = Normalise( accumulated[ key ] );
	}
}

void Mesh::Append( const Mesh& other, const Mat4& transform )
{
	const uint32_t base = Mark();
	vertices.reserve( vertices.size() + other.vertices.size() );
	for( const Vertex& v : other.vertices )
		AddVertex( ( transform * Vec4( v.position, 1.0f ) ).xyz(),
		           Normalise( transform.TransformDirection( v.normal ) ), v.colour );

	indices.reserve( indices.size() + other.indices.size() );
	for( uint32_t i : other.indices )
		indices.push_back( base + i );
}

} // namespace idler
