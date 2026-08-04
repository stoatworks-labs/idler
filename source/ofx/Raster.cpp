#include "Raster.h"

#include <algorithm>
#include <cmath>

namespace idler
{
namespace
{

/// A vertex after the vertex shader: clip position plus everything the
/// fragment stage interpolates. Same set as the `out` block in
/// SceneVertexShader, and in the same order, so the two can be read side by
/// side.
struct Varying
{
	Vec4 clip;         ///< gl_Position
	Vec3 normal;       ///< fNormal, view space
	// fColour, as four floats rather than a Vec4: Vec4 in this codebase is a
	// plain carrier with no arithmetic on it, and giving it some just for this
	// file would put a second, subtly different vector type in front of every
	// saver that only ever needed the carrier.
	float red = 0.0f, green = 0.0f, blue = 0.0f, alpha = 0.0f;
	Vec3 barycentric;  ///< fBarycentric
	float viewDepth = 0.0f;
};

float Mix( float a, float b, float t )
{
	return a + ( b - a ) * t;
}

Varying Lerp( const Varying& a, const Varying& b, float t )
{
	Varying out;
	out.clip        = Vec4( Mix( a.clip.x, b.clip.x, t ), Mix( a.clip.y, b.clip.y, t ),
	                        Mix( a.clip.z, b.clip.z, t ), Mix( a.clip.w, b.clip.w, t ) );
	out.normal      = a.normal + ( b.normal - a.normal ) * t;
	out.red         = Mix( a.red, b.red, t );
	out.green       = Mix( a.green, b.green, t );
	out.blue        = Mix( a.blue, b.blue, t );
	out.alpha       = Mix( a.alpha, b.alpha, t );
	out.barycentric = a.barycentric + ( b.barycentric - a.barycentric ) * t;
	out.viewDepth   = Mix( a.viewDepth, b.viewDepth, t );
	return out;
}

/**
    Clip a triangle against the near plane, and only against the near plane.

    The other five frustum planes need no clipping because the scan converter
    is bounded by the triangle's screen-space bounding box intersected with the
    frame -- geometry off the side of the screen simply produces no pixels. The
    near plane is different in kind: a vertex at or behind the eye has w <= 0,
    and dividing by it does not put the vertex somewhere wrong, it puts it
    somewhere meaningless. 3D Maze walks the camera down a corridor with walls
    passing either side of it, so this fires on most frames of that saver, and
    skipping it turns the corridor inside out rather than merely clipping it.

    Sutherland-Hodgman against `w > kNearEpsilon`, emitting a fan.
*/
constexpr float kNearEpsilon = 1e-5f;

int ClipNear( const Varying in[ 3 ], Varying out[ 4 ] )
{
	int count = 0;
	for( int i = 0; i < 3; ++i )
	{
		const Varying& current = in[ i ];
		const Varying& next    = in[ ( i + 1 ) % 3 ];

		const bool currentInside = current.clip.w > kNearEpsilon;
		const bool nextInside    = next.clip.w > kNearEpsilon;

		if( currentInside )
			out[ count++ ] = current;

		if( currentInside != nextInside )
		{
			const float t = ( kNearEpsilon - current.clip.w ) / ( next.clip.w - current.clip.w );
			out[ count++ ] = Lerp( current, next, t );
		}
	}
	return count;
}

}  // namespace

void Rasterise( const Scene& scene, float* rgba, int width, int height, float edgeWidth )
{
	if( width <= 0 || height <= 0 )
		return;

	const size_t pixels = static_cast< size_t >( width ) * static_cast< size_t >( height );

	// glClearColor + glClearDepth( 1.0 ). The background is already
	// premultiplied by the caller, exactly as the GL path requires.
	for( size_t i = 0; i < pixels; ++i )
	{
		rgba[ i * 4 + 0 ] = scene.background.x;
		rgba[ i * 4 + 1 ] = scene.background.y;
		rgba[ i * 4 + 2 ] = scene.background.z;
		rgba[ i * 4 + 3 ] = scene.background.w;
	}

	std::vector< float > depth( pixels, 1.0f );

	const std::vector< Vertex >& vertices  = scene.mesh.vertices;
	const std::vector< uint32_t >& indices = scene.mesh.indices;
	const bool wireframe                   = scene.shading == Shading::Wireframe;
	const bool lit                         = scene.shading == Shading::Lit;
	const bool fog                         = scene.fogEnd > scene.fogStart;

	// The vertex stage, once per vertex rather than once per use. mat3( View )
	// is the normal matrix for the same reason the shader says it is: nothing
	// here scales non-uniformly.
	std::vector< Varying > transformed( vertices.size() );
	for( size_t i = 0; i < vertices.size(); ++i )
	{
		const Vertex& v = vertices[ i ];

		const Vec4 viewPosition = scene.view * Vec4( v.position.x, v.position.y, v.position.z, 1.0f );

		Varying& out  = transformed[ i ];
		out.clip      = scene.proj * viewPosition;
		out.normal    = scene.view.TransformDirection( v.normal );
		out.red       = v.colour.x;
		out.green     = v.colour.y;
		out.blue      = v.colour.z;
		out.alpha     = v.colour.w;
		out.viewDepth = -viewPosition.z;
		// barycentric is assigned per TRIANGLE CORNER below, not here: a shared
		// vertex cannot carry three different corners at once, which is exactly
		// why the GL path de-indexes the mesh for wireframe and only for
		// wireframe. Doing it per corner costs nothing here.
	}

	// Screen-space position and the reciprocal of w, which is what makes the
	// interpolation perspective-correct: attributes are interpolated as
	// (attribute / w) and divided by the interpolated (1 / w) at the end.
	struct Raster
	{
		float x, y, z, invW;
	};

	auto project = [ & ]( const Varying& v ) {
		Raster r;
		const float invW = 1.0f / v.clip.w;
		// NDC, then the viewport transform. y is flipped because the buffer's
		// first row is the top of the picture and GL's is the bottom.
		r.x    = ( v.clip.x * invW * 0.5f + 0.5f ) * static_cast< float >( width );
		r.y    = ( 0.5f - v.clip.y * invW * 0.5f ) * static_cast< float >( height );
		r.z    = v.clip.z * invW * 0.5f + 0.5f;  // glDepthRange( 0, 1 )
		r.invW = invW;
		return r;
	};

	Varying clipped[ 4 ];

	for( size_t base = 0; base + 2 < indices.size(); base += 3 )
	{
		Varying source[ 3 ] = { transformed[ indices[ base + 0 ] ],
		                        transformed[ indices[ base + 1 ] ],
		                        transformed[ indices[ base + 2 ] ] };

		source[ 0 ].barycentric = Vec3( 1.0f, 0.0f, 0.0f );
		source[ 1 ].barycentric = Vec3( 0.0f, 1.0f, 0.0f );
		source[ 2 ].barycentric = Vec3( 0.0f, 0.0f, 1.0f );

		const int count = ClipNear( source, clipped );
		if( count < 3 )
			continue;

		for( int fan = 2; fan < count; ++fan )
		{
			const Varying* tri[ 3 ] = { &clipped[ 0 ], &clipped[ fan - 1 ], &clipped[ fan ] };

			const Raster a = project( *tri[ 0 ] );
			const Raster b = project( *tri[ 1 ] );
			const Raster c = project( *tri[ 2 ] );

			// Signed area in window coordinates. Its sign is gl_FrontFacing:
			// GL's default front face is counter-clockwise in window space,
			// and the y flip above means a CCW triangle comes out negative
			// here. Nothing is culled -- the sign only decides whether the
			// normal is flipped for lighting.
			const float area = ( b.x - a.x ) * ( c.y - a.y ) - ( c.x - a.x ) * ( b.y - a.y );
			if( area == 0.0f )
				continue;
			const bool frontFacing = area < 0.0f;

			const float invArea = 1.0f / area;

			int minX = static_cast< int >( std::floor( std::min( { a.x, b.x, c.x } ) ) );
			int maxX = static_cast< int >( std::ceil( std::max( { a.x, b.x, c.x } ) ) );
			int minY = static_cast< int >( std::floor( std::min( { a.y, b.y, c.y } ) ) );
			int maxY = static_cast< int >( std::ceil( std::max( { a.y, b.y, c.y } ) ) );

			minX = std::max( minX, 0 );
			minY = std::max( minY, 0 );
			maxX = std::min( maxX, width - 1 );
			maxY = std::min( maxY, height - 1 );

			// Barycentric weights at a sample point, as GL computes coverage:
			// the pixel centre.
			auto weights = [ & ]( float px, float py, float& w0, float& w1, float& w2 ) {
				w0 = ( ( b.x - px ) * ( c.y - py ) - ( c.x - px ) * ( b.y - py ) ) * invArea;
				w1 = ( ( c.x - px ) * ( a.y - py ) - ( a.x - px ) * ( c.y - py ) ) * invArea;
				w2 = 1.0f - w0 - w1;
			};

			for( int y = minY; y <= maxY; ++y )
			{
				for( int x = minX; x <= maxX; ++x )
				{
					const float px = static_cast< float >( x ) + 0.5f;
					const float py = static_cast< float >( y ) + 0.5f;

					float w0, w1, w2;
					weights( px, py, w0, w1, w2 );
					if( w0 < 0.0f || w1 < 0.0f || w2 < 0.0f )
						continue;

					const float z = w0 * a.z + w1 * b.z + w2 * c.z;
					const size_t index = static_cast< size_t >( y ) * static_cast< size_t >( width )
					                     + static_cast< size_t >( x );

					// GL_LESS, and only when the scene asked for it.
					if( scene.depthTest && !( z < depth[ index ] ) )
						continue;

					// Perspective-correct: interpolate attribute/w, then divide
					// by the interpolated 1/w.
					const float invW = w0 * a.invW + w1 * b.invW + w2 * c.invW;
					if( invW <= 0.0f )
						continue;
					const float w = 1.0f / invW;

					const float p0 = w0 * a.invW * w;
					const float p1 = w1 * b.invW * w;
					const float p2 = w2 * c.invW * w;

					float red   = tri[ 0 ]->red * p0 + tri[ 1 ]->red * p1 + tri[ 2 ]->red * p2;
					float green = tri[ 0 ]->green * p0 + tri[ 1 ]->green * p1 + tri[ 2 ]->green * p2;
					float blue  = tri[ 0 ]->blue * p0 + tri[ 1 ]->blue * p1 + tri[ 2 ]->blue * p2;
					float alpha = tri[ 0 ]->alpha * p0 + tri[ 1 ]->alpha * p1 + tri[ 2 ]->alpha * p2;

					if( lit || wireframe )
					{
						Vec3 n = Normalise( tri[ 0 ]->normal * p0 + tri[ 1 ]->normal * p1
						                    + tri[ 2 ]->normal * p2 );

						// Two-sided, for the reason the shader gives: a visible
						// back face means the near plane has cut into a closed
						// solid, which happens constantly inside 3D Maze.
						if( !frontFacing )
							n = -n;

						const float facing = Dot( n, -scene.lightDirection );

						float diffuse = std::max( facing, 0.0f );
						// Wrapped rather than clamped at the terminator -- the
						// same expression, in the same order, as the shader.
						diffuse = diffuse * 0.75f + 0.25f * ( 0.5f + 0.5f * facing );

						const float shade = scene.ambient + ( 1.0f - scene.ambient ) * diffuse;
						red *= shade;
						green *= shade;
						blue *= shade;
					}

					if( wireframe )
					{
						const Vec3 bary = tri[ 0 ]->barycentric * p0 + tri[ 1 ]->barycentric * p1
						                  + tri[ 2 ]->barycentric * p2;

						// fwidth(), by finite difference across one pixel --
						// which is what a GPU computes it as. Sampling the
						// neighbours through the same perspective-correct path
						// matters: an affine difference is visibly wrong on a
						// triangle seen at a shallow angle, which in 3D
						// FlowerBox is most of them.
						auto baryAt = [ & ]( float sx, float sy, Vec3& outBary ) {
							float u0, u1, u2;
							weights( sx, sy, u0, u1, u2 );
							const float iw = u0 * a.invW + u1 * b.invW + u2 * c.invW;
							if( iw <= 0.0f )
								return false;
							const float ww = 1.0f / iw;
							outBary        = tri[ 0 ]->barycentric * ( u0 * a.invW * ww )
							          + tri[ 1 ]->barycentric * ( u1 * b.invW * ww )
							          + tri[ 2 ]->barycentric * ( u2 * c.invW * ww );
							return true;
						};

						Vec3 baryX = bary;
						Vec3 baryY = bary;
						baryAt( px + 1.0f, py, baryX );
						baryAt( px, py + 1.0f, baryY );

						const Vec3 delta( std::fabs( baryX.x - bary.x ) + std::fabs( baryY.x - bary.x ),
						                  std::fabs( baryX.y - bary.y ) + std::fabs( baryY.y - bary.y ),
						                  std::fabs( baryX.z - bary.z ) + std::fabs( baryY.z - bary.z ) );

						auto smoothstep = [] ( float edge1, float value ) {
							if( edge1 <= 0.0f )
								return 1.0f;
							const float t = std::min( std::max( value / edge1, 0.0f ), 1.0f );
							return t * t * ( 3.0f - 2.0f * t );
						};

						const float edge = 1.0f
						                   - std::min( { smoothstep( delta.x * edgeWidth, bary.x ),
						                                 smoothstep( delta.y * edgeWidth, bary.y ),
						                                 smoothstep( delta.z * edgeWidth, bary.z ) } );

						alpha *= edge;

						// discard, not a transparent write: the depth test is
						// on and a transparent fragment would still write depth
						// and punch a hole in whatever is behind it.
						if( alpha < 0.004f )
							continue;
					}

					if( fog )
					{
						const float viewDepth = tri[ 0 ]->viewDepth * p0 + tri[ 1 ]->viewDepth * p1
						                        + tri[ 2 ]->viewDepth * p2;
						const float amount = std::min(
						    std::max( ( viewDepth - scene.fogStart ) / ( scene.fogEnd - scene.fogStart ),
						              0.0f ),
						    1.0f );
						// Fades toward NOTHING rather than toward black, which
						// only works because the target is premultiplied.
						alpha *= 1.0f - amount;
					}

					// FragColour = vec4( rgb * alpha, alpha ), then
					// glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA ).
					const float sourceRed   = red * alpha;
					const float sourceGreen = green * alpha;
					const float sourceBlue  = blue * alpha;
					const float inverse     = 1.0f - alpha;

					float* pixel = rgba + index * 4;
					pixel[ 0 ]   = sourceRed + pixel[ 0 ] * inverse;
					pixel[ 1 ]   = sourceGreen + pixel[ 1 ] * inverse;
					pixel[ 2 ]   = sourceBlue + pixel[ 2 ] * inverse;
					pixel[ 3 ]   = alpha + pixel[ 3 ] * inverse;

					if( scene.depthTest )
						depth[ index ] = z;
				}
			}
		}
	}
}

}  // namespace idler
