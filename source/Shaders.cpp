#include "Shaders.h"

#include <string>

namespace idler
{

const char* SceneVertexShader()
{
	// 410 core: the highest GLSL macOS offers, and what the rest of the fleet
	// targets. Nothing here needs more.
	static const char* source = R"(#version 410 core

layout( location = 0 ) in vec3 vPosition;
layout( location = 1 ) in vec3 vNormal;
layout( location = 2 ) in vec4 vColour;
layout( location = 3 ) in vec3 vBarycentric;

uniform mat4 View;
uniform mat4 Proj;

out vec3 fNormal;
out vec4 fColour;
out vec3 fBarycentric;
out float fViewDepth;

void main()
{
	vec4 viewPosition = View * vec4( vPosition, 1.0 );

	// mat3( View ) is the correct normal matrix only because nothing here
	// scales non-uniformly -- the savers bake their scaling into the vertex
	// positions on the CPU. If one ever needs a non-uniform model scale it
	// needs the inverse transpose, and the symptom of skipping that is
	// lighting that slides across a stretched face.
	fNormal      = mat3( View ) * vNormal;
	fColour      = vColour;
	fBarycentric = vBarycentric;

	// Positive distance in front of the camera, for the fog. Negated because
	// the camera looks down -Z.
	fViewDepth = -viewPosition.z;

	gl_Position = Proj * viewPosition;
}
)";
	return source;
}

const char* SceneFragmentShader()
{
	static const char* source = R"(#version 410 core

in vec3 fNormal;
in vec4 fColour;
in vec3 fBarycentric;
in float fViewDepth;

out vec4 FragColour;

// 0 flat, 1 lit, 2 wireframe. An int rather than three programs because the
// branch is uniform across the draw call and costs nothing measurable, and
// three programs would be three things to keep in step.
uniform int ShadingMode;

// The direction the light travels, in VIEW space -- so the lighting does not
// swing round as a saver's camera orbits. A light fixed in world space reads as
// a searchlight following the object, which is not what any of these did.
uniform vec3 LightDirection;
uniform float Ambient;

// x = start, y = end, in view-space units. Disabled when y <= x.
uniform vec2 FogRange;

// Wireframe line half-width, in pixels.
uniform float EdgeWidth;

void main()
{
	vec3 rgb    = fColour.rgb;
	float alpha = fColour.a;

	if( ShadingMode != 0 )
	{
		vec3 n = normalize( fNormal );

		// Two-sided. The meshes here are closed solids, so a visible back face
		// means the near plane has cut into one -- which happens constantly in
		// 3D Maze, where the camera is inside the corridor. Lighting it by the
		// unflipped normal makes those fragments black and the wall looks
		// holed.
		if( !gl_FrontFacing )
			n = -n;

		float diffuse = max( dot( n, -LightDirection ), 0.0 );

		// Wrapped rather than clamped at the terminator. A pure Lambert on a
		// single light leaves half of every pipe dead black, and the original
		// used a second fill light to avoid exactly that; this is the cheap
		// equivalent.
		diffuse = diffuse * 0.75 + 0.25 * ( 0.5 + 0.5 * dot( n, -LightDirection ) );

		rgb *= Ambient + ( 1.0 - Ambient ) * diffuse;
	}

	if( ShadingMode == 2 )
	{
		// Distance to the nearest edge, in the barycentric's own units, scaled
		// by how fast it changes across a pixel -- which turns it into a
		// distance in pixels and keeps the line the same weight whatever the
		// triangle's size or the output resolution.
		vec3 delta = fwidth( fBarycentric );
		vec3 edges = smoothstep( vec3( 0.0 ), delta * EdgeWidth, fBarycentric );
		float edge = 1.0 - min( min( edges.x, edges.y ), edges.z );

		alpha *= edge;

		// Discard rather than write a transparent fragment: this runs with the
		// depth test on, and a fully transparent fragment still writes depth
		// and would punch a hole in whatever is behind it.
		if( alpha < 0.004 )
			discard;
	}

	if( FogRange.y > FogRange.x )
	{
		float fog = clamp( ( fViewDepth - FogRange.x ) / ( FogRange.y - FogRange.x ), 0.0, 1.0 );

		// Fades toward NOTHING, not toward black. That is what lets a corridor
		// recede into whatever is behind the plugin instead of into a black
		// rectangle the shape of the frame -- and it only works because the
		// target is premultiplied.
		alpha *= 1.0 - fog;
	}

	FragColour = vec4( rgb * alpha, alpha );
}
)";
	return source;
}

const char* CompositeVertexShader()
{
	// One triangle covering the frame, built from gl_VertexID. No vertex buffer
	// and no attributes -- but a core profile still refuses to draw with no
	// vertex array object bound at all, even when the shader sources nothing,
	// so the caller keeps an empty one around for this.
	static const char* source = R"(#version 410 core

out vec2 fUV;

void main()
{
	vec2 position = vec2( ( gl_VertexID << 1 ) & 2, gl_VertexID & 2 );
	fUV = position;
	gl_Position = vec4( position * 2.0 - 1.0, 0.0, 1.0 );
}
)";
	return source;
}

const char* CompositeFragmentShader( bool hasInput )
{
	static std::string sourceWithInput;
	static std::string sourceWithoutInput;

	std::string& cached = hasInput ? sourceWithInput : sourceWithoutInput;
	if( !cached.empty() )
		return cached.c_str();

	std::string source = "#version 410 core\n";
	if( hasInput )
		source += "#define HAS_INPUT 1\n";

	source += R"(
in vec2 fUV;
out vec4 FragColour;

// Premultiplied, from the off-screen target.
uniform sampler2D SceneTexture;

// 0 Over, 1 Reveal, 2 Hide, 3 Colourise. Only read when there is an input.
uniform int MaskMode;
uniform float MixAmount;

#ifdef HAS_INPUT
uniform sampler2D InputTexture;

// The input texture can be BIGGER than the picture -- the host hands over a
// power-of-two or pooled texture and says how much of it was really drawn.
// Sampling the whole thing pulls in undrawn padding down two edges.
uniform vec2 MaxUV;
#endif

void main()
{
	vec4 scene = texture( SceneTexture, fUV );

#ifdef HAS_INPUT
	// Half a texel inside, so that GL_LINEAR at the picture edge does not take
	// half its weight from the padding beyond MaxUV.
	vec2 inputUV = fUV * MaxUV;
	vec4 clip = texture( InputTexture, inputUV );

	vec4 result;
	if( MaskMode == 1 )// Reveal
	{
		result = clip * scene.a;
	}
	else if( MaskMode == 2 )// Hide
	{
		result = clip * ( 1.0 - scene.a );
	}
	else if( MaskMode == 3 )// Colourise
	{
		// Un-premultiply the saver's colour before tinting, or the tint is
		// darkened a second time by its own alpha at every soft edge.
		vec3 tint = scene.a > 0.0001 ? scene.rgb / scene.a : vec3( 0.0 );
		result = vec4( clip.rgb * tint, clip.a ) * scene.a;
	}
	else// Over
	{
		result = scene + clip * ( 1.0 - scene.a );
	}

	// Mix crossfades between the untouched clip and the result, so 0 is an
	// exact bypass in every mask mode -- including Hide, where "no effect" is
	// the clip and not transparency.
	FragColour = mix( clip, result, MixAmount );
#else
	// The source has nothing to composite against, so Mix is a straight fade
	// to transparent.
	FragColour = scene * MixAmount;
#endif
}
)";

	cached = source;
	return cached.c_str();
}

} // namespace idler
