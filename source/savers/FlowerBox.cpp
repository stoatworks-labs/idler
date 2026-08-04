#include <algorithm>
#include <vector>

#include "../Savers.h"

/**
    3D FlowerBox.

    A solid sitting on the spot, turning over, its surface swelling in and out
    between a box, a ball and a many-lobed flower.

    ## The scene controls, in this saver

    - **Complexity** -- how many lobes the flower grows. 2..9.
    - **Size** -- how big it is in frame.
    - **Length** -- how far the morph travels: at zero it is a sphere that
      never changes, at one it goes all the way to the spiky extreme.
    - **Density** -- surface resolution, 12..48 segments. This is the one place
      Density means "how much geometry" rather than "how many things", because
      there is only ever one of these.
    - **Variation** -- how much the two morph terms differ in rate, which is
      what stops the shape returning to the same pose every cycle.
    - **Line Width** -- the wireframe weight, which is the shading this saver
      most rewards.

    ## The shape

    A sphere whose radius is a function of the direction you look in:

        r(theta, phi) = 1 + A * sin(m*theta) * sin(n*phi) + B * boxiness

    The first term is the flower -- `m` lobes around and `n` up -- and the
    second pushes toward a cube by taking the Chebyshev distance instead of the
    Euclidean one. Both amplitudes are driven by slow sines at rates that do not
    divide into each other, so the shape wanders through its space rather than
    looping.

    ## Why the normals are accumulated rather than derived

    The analytic normal of that surface is available -- it is a partial
    derivative of the radius function in two variables -- and it is not used.
    At the extremes of the morph the surface folds through itself, and where it
    does, the analytic normal is correct and useless: it points into the fold,
    and the shading goes to pieces exactly where the shape is most interesting.
    Accumulating face normals gives the surface the geometry actually has.
*/
namespace idler
{
namespace
{
class FlowerBox : public Saver
{
public:
	void Build( const Settings& s, Scene& scene ) override
	{
		SetCamera( s, scene );

		const int segments = 12 + static_cast< int >( s.density * 36.0f + 0.5f );
		const int rings    = segments / 2 + 2;

		const float lobes = 2.0f + s.complexity * 7.0f;
		const float reach = s.length * ( 0.55f + s.audioLevel * s.audioSize * 0.5f );
		const float scale = 0.4f + s.size * 0.9f;

		// Two rates that do not divide into each other, so the pair does not
		// return to the same pose on a short cycle. Ratios like 3:2 look
		// deliberately periodic within about twenty seconds.
		const float rateA = 0.11f * ( 0.6f + s.variation );
		const float rateB = 0.077f;

		const float flower  = std::sin( s.time * rateA ) * reach;
		const float boxness = ( std::sin( s.time * rateB ) * 0.5f + 0.5f ) * reach;

		const uint32_t start = scene.mesh.Mark();

		for( int r = 0; r <= rings; ++r )
		{
			const float phi = kPi * static_cast< float >( r ) / static_cast< float >( rings );
			const float sp = std::sin( phi ), cp = std::cos( phi );

			for( int a = 0; a <= segments; ++a )
			{
				const float theta = kTwoPi * static_cast< float >( a % segments ) / static_cast< float >( segments );

				const Vec3 direction{ sp * std::cos( theta ), cp, sp * std::sin( theta ) };

				// The flower term.
				float radius = 1.0f + flower * std::sin( lobes * theta ) * std::sin( lobes * phi );

				// The box term. Dividing by the Chebyshev norm pushes a point
				// on the unit sphere out onto the surface of the unit cube, so
				// interpolating between 1 and that is a sphere-to-cube morph
				// that stays continuous at the corners.
				const float chebyshev = std::max( { std::fabs( direction.x ), std::fabs( direction.y ),
				                                    std::fabs( direction.z ) } );
				if( chebyshev > 1e-4f )
					radius = radius * ( 1.0f - boxness ) + ( radius / chebyshev ) * boxness;

				const Vec3 position = direction * ( radius * scale );

				// The colour follows the direction rather than the position, so
				// it stays put on the surface as the shape swells instead of
				// sliding about.
				const Vec3 classic = HsvToRgb( theta / kTwoPi + s.time * 0.03f,
				                               0.55f + 0.35f * std::fabs( cp ), 1.0f );
				const Vec4 colour = s.Colour( classic, a, segments );

				scene.mesh.AddVertex( position, direction, colour );
			}
		}

		const uint32_t stride = static_cast< uint32_t >( segments ) + 1;
		for( int r = 0; r < rings; ++r )
			for( int a = 0; a < segments; ++a )
			{
				const uint32_t v = start + static_cast< uint32_t >( r ) * stride + static_cast< uint32_t >( a );
				scene.mesh.AddQuad( v, v + stride, v + stride + 1, v + 1 );
			}

		scene.mesh.SmoothNormals( start );
	}

	size_t ExpectedTriangles( const Settings& s ) const override
	{
		const int segments = 12 + static_cast< int >( s.density * 36.0f + 0.5f );
		return static_cast< size_t >( segments ) * static_cast< size_t >( segments / 2 + 2 ) * 2;
	}

private:
	static void SetCamera( const Settings& s, Scene& scene )
	{
		scene.depthTest = true;
		scene.shading   = ( s.shading == Shading::Flat ) ? Shading::Lit : s.shading;

		const float distance = 2.4f + s.camDistance * 3.0f;
		const float angle    = s.time * 0.13f;

		const Vec3 eye{ std::sin( angle ) * distance,
		                std::sin( s.camTilt ) * distance,
		                std::cos( angle ) * distance };

		scene.view = Mat4::LookAt( eye, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } );
		scene.proj = Mat4::Perspective( s.fov, std::max( 0.01f, s.aspect ), 0.05f, 40.0f );

		if( s.fog > 0.001f )
		{
			scene.fogStart = distance * 0.7f;
			scene.fogEnd   = distance * ( 1.4f + ( 1.0f - s.fog ) * 2.0f );
		}
	}
};
} // namespace

std::unique_ptr< Saver > MakeFlowerBox()
{
	return std::make_unique< FlowerBox >();
}

} // namespace idler
