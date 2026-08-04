#pragma once

#include <cmath>

/**
    Just enough linear algebra, and no more.

    Idler is the first plugin in the fleet that needs a real camera: 3D Maze
    walks down a corridor, 3D Pipes grows into a box you are looking into, and
    both want perspective and a depth test. Everything else in the fleet works
    in frame space, where a `vec2` and a bit of trigonometry is the whole story.

    This is deliberately not a maths library. It is the operations those savers
    actually use, written out so that the shading, the harness and the eventual
    software rasteriser in the OFX build can all share one definition of what
    "the model-view-projection matrix" means. A mismatch there is invisible on a
    static frame and shows up as the picture disagreeing with itself between
    hosts.

    Column-major, like GL, so `glUniformMatrix4fv` takes `m.m` with
    `transpose = GL_FALSE`. `Mat4::operator*` applies the right-hand matrix
    first, so `proj * view * model` reads in the order it is applied, backwards.
*/
namespace idler
{

struct Vec2
{
	float x = 0.0f, y = 0.0f;

	Vec2() = default;
	Vec2( float x, float y ) : x( x ), y( y ) {}

	Vec2 operator+( const Vec2& o ) const { return { x + o.x, y + o.y }; }
	Vec2 operator-( const Vec2& o ) const { return { x - o.x, y - o.y }; }
	Vec2 operator*( float s ) const { return { x * s, y * s }; }

	Vec2& operator+=( const Vec2& o ) { x += o.x; y += o.y; return *this; }
};

struct Vec3
{
	float x = 0.0f, y = 0.0f, z = 0.0f;

	Vec3() = default;
	Vec3( float x, float y, float z ) : x( x ), y( y ), z( z ) {}
	explicit Vec3( float s ) : x( s ), y( s ), z( s ) {}

	Vec3 operator+( const Vec3& o ) const { return { x + o.x, y + o.y, z + o.z }; }
	Vec3 operator-( const Vec3& o ) const { return { x - o.x, y - o.y, z - o.z }; }
	Vec3 operator*( float s ) const { return { x * s, y * s, z * s }; }
	Vec3 operator*( const Vec3& o ) const { return { x * o.x, y * o.y, z * o.z }; }
	Vec3 operator-() const { return { -x, -y, -z }; }

	Vec3& operator+=( const Vec3& o ) { x += o.x; y += o.y; z += o.z; return *this; }
	Vec3& operator*=( float s ) { x *= s; y *= s; z *= s; return *this; }

	bool operator==( const Vec3& o ) const { return x == o.x && y == o.y && z == o.z; }
};

inline float Dot( const Vec3& a, const Vec3& b )
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 Cross( const Vec3& a, const Vec3& b )
{
	return { a.y * b.z - a.z * b.y,
	         a.z * b.x - a.x * b.z,
	         a.x * b.y - a.y * b.x };
}

inline float Length( const Vec3& v )
{
	return std::sqrt( Dot( v, v ) );
}

/// Normalise, tolerating a zero vector rather than returning NaNs.
///
/// The savers generate a lot of normals from cross products of geometry they
/// built themselves, and a degenerate triangle -- a zero-radius ring cap, a
/// pipe elbow whose two segments are collinear -- produces a zero cross
/// product. A NaN normal poisons the lighting for the whole draw call, not
/// just that triangle, because it propagates through the smooth-normal
/// accumulation into every vertex that shares the position.
inline Vec3 Normalise( const Vec3& v )
{
	const float len = Length( v );
	if( len < 1e-12f )
		return { 0.0f, 0.0f, 1.0f };
	return v * ( 1.0f / len );
}

inline Vec3 Lerp( const Vec3& a, const Vec3& b, float t )
{
	return a + ( b - a ) * t;
}

struct Vec4
{
	float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

	Vec4() = default;
	Vec4( float x, float y, float z, float w ) : x( x ), y( y ), z( z ), w( w ) {}
	Vec4( const Vec3& v, float w ) : x( v.x ), y( v.y ), z( v.z ), w( w ) {}

	Vec3 xyz() const { return { x, y, z }; }
};

/**
    A 4x4 matrix, column-major.

    `m[ c * 4 + r ]` is row r of column c -- the layout GL wants, so it can be
    handed to `glUniformMatrix4fv` untransposed. Written out rather than
    wrapped because the OFX build's software rasteriser needs to walk the same
    numbers on the CPU.
*/
struct Mat4
{
	float m[ 16 ] = {};

	static Mat4 Identity()
	{
		Mat4 r;
		r.m[ 0 ] = r.m[ 5 ] = r.m[ 10 ] = r.m[ 15 ] = 1.0f;
		return r;
	}

	static Mat4 Translate( const Vec3& t )
	{
		Mat4 r = Identity();
		r.m[ 12 ] = t.x;
		r.m[ 13 ] = t.y;
		r.m[ 14 ] = t.z;
		return r;
	}

	static Mat4 Scale( const Vec3& s )
	{
		Mat4 r = Identity();
		r.m[ 0 ]  = s.x;
		r.m[ 5 ]  = s.y;
		r.m[ 10 ] = s.z;
		return r;
	}

	static Mat4 RotateX( float radians )
	{
		const float c = std::cos( radians ), s = std::sin( radians );
		Mat4 r = Identity();
		r.m[ 5 ] = c;  r.m[ 6 ]  = s;
		r.m[ 9 ] = -s; r.m[ 10 ] = c;
		return r;
	}

	static Mat4 RotateY( float radians )
	{
		const float c = std::cos( radians ), s = std::sin( radians );
		Mat4 r = Identity();
		r.m[ 0 ] = c; r.m[ 2 ]  = -s;
		r.m[ 8 ] = s; r.m[ 10 ] = c;
		return r;
	}

	static Mat4 RotateZ( float radians )
	{
		const float c = std::cos( radians ), s = std::sin( radians );
		Mat4 r = Identity();
		r.m[ 0 ] = c;  r.m[ 1 ] = s;
		r.m[ 4 ] = -s; r.m[ 5 ] = c;
		return r;
	}

	/// A rotation taking +Z onto `dir`. Used wherever a saver has a direction
	/// and needs an orientation: a pipe segment, a maze corridor, a star's
	/// motion blur streak.
	///
	/// The up vector is chosen away from `dir` rather than fixed, because a
	/// pipe travelling straight up is exactly the case a fixed +Y up vector
	/// degenerates on -- and pipes travel straight up constantly.
	static Mat4 AlignZTo( const Vec3& dir )
	{
		const Vec3 f = Normalise( dir );
		const Vec3 hint = ( std::fabs( f.y ) > 0.9f ) ? Vec3( 1.0f, 0.0f, 0.0f )
		                                              : Vec3( 0.0f, 1.0f, 0.0f );
		const Vec3 r = Normalise( Cross( hint, f ) );
		const Vec3 u = Cross( f, r );

		Mat4 out = Identity();
		out.m[ 0 ] = r.x; out.m[ 1 ] = r.y; out.m[ 2 ]  = r.z;
		out.m[ 4 ] = u.x; out.m[ 5 ] = u.y; out.m[ 6 ]  = u.z;
		out.m[ 8 ] = f.x; out.m[ 9 ] = f.y; out.m[ 10 ] = f.z;
		return out;
	}

	static Mat4 LookAt( const Vec3& eye, const Vec3& centre, const Vec3& up )
	{
		const Vec3 f = Normalise( centre - eye );
		const Vec3 s = Normalise( Cross( f, up ) );
		const Vec3 u = Cross( s, f );

		Mat4 r = Identity();
		r.m[ 0 ] = s.x; r.m[ 4 ] = s.y; r.m[ 8 ]  = s.z;
		r.m[ 1 ] = u.x; r.m[ 5 ] = u.y; r.m[ 9 ]  = u.z;
		r.m[ 2 ] = -f.x; r.m[ 6 ] = -f.y; r.m[ 10 ] = -f.z;
		r.m[ 12 ] = -Dot( s, eye );
		r.m[ 13 ] = -Dot( u, eye );
		r.m[ 14 ] = Dot( f, eye );
		return r;
	}

	/// Perspective projection. `fovY` in radians, vertical.
	///
	/// Vertical rather than horizontal so that a saver composed for the frame
	/// height keeps its framing as the composition gets wider -- which is the
	/// behaviour a VJ expects when the same clip is dropped on a 16:9 screen
	/// and a 4:1 LED banner.
	static Mat4 Perspective( float fovY, float aspect, float nearZ, float farZ )
	{
		const float t = 1.0f / std::tan( fovY * 0.5f );
		Mat4 r;
		r.m[ 0 ]  = t / aspect;
		r.m[ 5 ]  = t;
		r.m[ 10 ] = ( farZ + nearZ ) / ( nearZ - farZ );
		r.m[ 11 ] = -1.0f;
		r.m[ 14 ] = ( 2.0f * farZ * nearZ ) / ( nearZ - farZ );
		return r;
	}

	/// Orthographic projection, for the 2D savers.
	///
	/// They draw in the same pipeline as the 3D ones -- same shader, same
	/// vertex format, same depth buffer with the test switched off -- so that
	/// there is one renderer to get right rather than two.
	static Mat4 Ortho( float l, float r_, float b, float t, float n, float f )
	{
		Mat4 r = Identity();
		r.m[ 0 ]  = 2.0f / ( r_ - l );
		r.m[ 5 ]  = 2.0f / ( t - b );
		r.m[ 10 ] = -2.0f / ( f - n );
		r.m[ 12 ] = -( r_ + l ) / ( r_ - l );
		r.m[ 13 ] = -( t + b ) / ( t - b );
		r.m[ 14 ] = -( f + n ) / ( f - n );
		return r;
	}

	Mat4 operator*( const Mat4& o ) const
	{
		Mat4 r;
		for( int c = 0; c < 4; ++c )
			for( int row = 0; row < 4; ++row )
			{
				float sum = 0.0f;
				for( int k = 0; k < 4; ++k )
					sum += m[ k * 4 + row ] * o.m[ c * 4 + k ];
				r.m[ c * 4 + row ] = sum;
			}
		return r;
	}

	Vec4 operator*( const Vec4& v ) const
	{
		return {
			m[ 0 ] * v.x + m[ 4 ] * v.y + m[ 8 ]  * v.z + m[ 12 ] * v.w,
			m[ 1 ] * v.x + m[ 5 ] * v.y + m[ 9 ]  * v.z + m[ 13 ] * v.w,
			m[ 2 ] * v.x + m[ 6 ] * v.y + m[ 10 ] * v.z + m[ 14 ] * v.w,
			m[ 3 ] * v.x + m[ 7 ] * v.y + m[ 11 ] * v.z + m[ 15 ] * v.w
		};
	}

	/// Transform a direction: the rotation and scale, without the translation.
	Vec3 TransformDirection( const Vec3& v ) const
	{
		return {
			m[ 0 ] * v.x + m[ 4 ] * v.y + m[ 8 ]  * v.z,
			m[ 1 ] * v.x + m[ 5 ] * v.y + m[ 9 ]  * v.z,
			m[ 2 ] * v.x + m[ 6 ] * v.y + m[ 10 ] * v.z
		};
	}
};

/// HSV to RGB, hue in turns rather than degrees.
///
/// Turns because every hue in this plugin comes from a phase or a 0..1
/// parameter, and multiplying by 360 only to divide by it again is a step at
/// which somebody eventually writes 260.
inline Vec3 HsvToRgb( float hueTurns, float s, float v )
{
	const float h = ( hueTurns - std::floor( hueTurns ) ) * 6.0f;
	const int i   = static_cast< int >( h );
	const float f = h - static_cast< float >( i );
	const float p = v * ( 1.0f - s );
	const float q = v * ( 1.0f - s * f );
	const float t = v * ( 1.0f - s * ( 1.0f - f ) );

	switch( i )
	{
	case 0:  return { v, t, p };
	case 1:  return { q, v, p };
	case 2:  return { p, v, t };
	case 3:  return { p, q, v };
	case 4:  return { t, p, v };
	default: return { v, p, q };
	}
}

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

/// A triangle wave on 0..1 with period 1, which is how every bounce in this
/// plugin is expressed.
///
/// A bounce written as a triangle wave is a pure function of time; written as
/// "add velocity, test for a wall, negate" it is an integration whose speed is
/// whatever the host's frame rate happened to be. Resolume's frame rate drops
/// when the show gets heavy, and Mystify slowing down when the projection load
/// goes up is exactly the failure this avoids.
inline float TriangleWave( float t )
{
	const float f = t - std::floor( t );
	return f < 0.5f ? f * 2.0f : 2.0f - f * 2.0f;
}

} // namespace idler
