#include <algorithm>

#include "../Savers.h"
#include "Teapot.h"

/**
    3D Flying Objects.

    Lit solids tumbling slowly through space, drifting past the camera and
    round again.

    ## The scene controls, in this saver

    - **Density** -- how many objects. 1..24.
    - **Complexity** -- which solids are in the mix. Turning it up brings in
      the more elaborate ones, so the low end is boxes and balls and the high
      end has toruses and teapots in it.
    - **Size** -- how big they are.
    - **Length** -- how deep the volume they drift through is, which is what
      controls whether they pass close by or stay at a distance.
    - **Variation** -- how much they differ in size and tumble rate.
    - **Line Width** -- the wireframe weight.

    ## Everything is a pure function of time again

    Each object's position is three sines at rates drawn from its own hash --
    a Lissajous in three dimensions, which never repeats exactly and never
    needs a bounds test, because a sine is already bounded. Its orientation is
    three angles that advance linearly.

    That is the whole saver. There is no integration and no collision, which is
    what makes it seekable and frame-rate independent like the rest.

    ## Why they are allowed to pass through each other

    They do intersect, and it is left alone. Keeping a dozen tumbling solids
    apart means either a collision response -- which is state, and would cost
    the seekability -- or spacing them so far apart that the frame is mostly
    empty. The original let them overlap too.
*/
namespace idler
{
namespace
{
enum class Solid
{
	Box = 0,
	Sphere,
	Cylinder,
	Torus,
	Teapot,

	Count
};

class FlyingObjects : public Saver
{
public:
	void Build( const Settings& s, Scene& scene ) override
	{
		const float depth = 3.0f + s.length * 9.0f;
		SetCamera( s, scene, depth );

		const int count = CountFromDensity( s.density, 1, 24 );

		// Complexity opens up the shape list rather than picking one shape, so
		// that moving the slider adds variety instead of swapping the picture
		// for a different picture.
		const int available = 1 + static_cast< int >( s.complexity * ( static_cast< float >( Solid::Count ) - 1.0f ) + 0.5f );

		const float baseSize = ( 0.15f + s.size * 0.5f ) * ( 1.0f + s.audioLevel * s.audioSize );

		for( int i = 0; i < count; ++i )
		{
			const uint32_t h = Hash2( static_cast< uint32_t >( i ), s.seed );

			// A Lissajous in three dimensions. The rates are close to each
			// other but not equal, so the paths are all of a family without any
			// two objects sharing one.
			const float rx = 0.05f + Unit( Hash2( h, 0x11U ) ) * 0.06f;
			const float ry = 0.05f + Unit( Hash2( h, 0x22U ) ) * 0.06f;
			const float rz = 0.04f + Unit( Hash2( h, 0x33U ) ) * 0.05f;

			const float px = Unit( Hash2( h, 0x44U ) ) * kTwoPi;
			const float py = Unit( Hash2( h, 0x55U ) ) * kTwoPi;
			const float pz = Unit( Hash2( h, 0x66U ) ) * kTwoPi;

			const float spread = 1.6f + s.length * 2.5f;

			const Vec3 position{ std::sin( s.time * rx * kTwoPi + px ) * spread * s.aspect * 0.6f,
			                     std::sin( s.time * ry * kTwoPi + py ) * spread * 0.6f,
			                     -depth * 0.5f + std::sin( s.time * rz * kTwoPi + pz ) * depth * 0.35f };

			const float tumble = ( 0.4f + Unit( Hash2( h, 0x77U ) ) * s.variation * 2.0f );
			const Mat4 orientation = Mat4::RotateY( s.time * tumble * 0.7f ) *
			                         Mat4::RotateX( s.time * tumble * 0.43f ) *
			                         Mat4::RotateZ( s.time * tumble * 0.29f );

			const float size = baseSize * ( 1.0f - s.variation * 0.6f * Unit( Hash2( h, 0x88U ) ) );

			const Mat4 place = Mat4::Translate( position ) * orientation;

			const Solid solid  = static_cast< Solid >( static_cast< int >( Hash2( h, 0x99U ) % static_cast< uint32_t >( available ) ) );
			const Vec3 classic = HsvToRgb( Unit( Hash2( h, 0xAAU ) ), 0.7f, 1.0f );
			const Vec4 colour  = s.Colour( classic, i, count );

			switch( solid )
			{
			case Solid::Box:
				scene.mesh.AddTransformedBox( place, Vec3( size ), colour );
				break;

			case Solid::Sphere:
				scene.mesh.AddSphere( place, size, 10, 16, colour );
				break;

			case Solid::Cylinder:
				// Placed back half its own length so it tumbles about its
				// middle. AddCylinder builds from z = 0 forwards, so without
				// this it would swing round one end like a thrown baton.
				scene.mesh.AddCylinder( place * Mat4::Translate( { 0.0f, 0.0f, -size } ),
				                        size * 0.6f, size * 2.0f, 14, colour, true, true );
				break;

			case Solid::Torus:
				scene.mesh.AddTorus( place, size, size * 0.38f, 20, 10, colour );
				break;

			case Solid::Teapot:
			default:
				AddTeapot( scene.mesh, place, size * 1.3f, colour );
				break;
			}
		}
	}

	size_t ExpectedTriangles( const Settings& s ) const override
	{
		// A torus is the worst case at 400 triangles; the estimate is for the
		// harness's buffer sizing, not a budget.
		return static_cast< size_t >( CountFromDensity( s.density, 1, 24 ) ) * 400;
	}

private:
	static void SetCamera( const Settings& s, Scene& scene, float depth )
	{
		scene.depthTest = true;
		scene.shading   = ( s.shading == Shading::Flat ) ? Shading::Lit : s.shading;

		const float back = 0.5f + s.camDistance * 3.0f;

		scene.view = Mat4::LookAt( { 0.0f, std::sin( s.camTilt ) * back, back },
		                           { 0.0f, 0.0f, -depth * 0.4f }, { 0.0f, 1.0f, 0.0f } );
		scene.proj = Mat4::Perspective( s.fov, std::max( 0.01f, s.aspect ), 0.05f, depth * 4.0f + 10.0f );

		if( s.fog > 0.001f )
		{
			// Deep, so objects fade out at the back of the volume rather than
			// disappearing at the far plane.
			scene.fogStart = back + depth * ( 0.2f + ( 1.0f - s.fog ) * 0.6f );
			scene.fogEnd   = back + depth * 1.3f;
		}
	}
};
} // namespace

std::unique_ptr< Saver > MakeFlyingObjects()
{
	return std::make_unique< FlyingObjects >();
}

} // namespace idler
