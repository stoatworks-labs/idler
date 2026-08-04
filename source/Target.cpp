#include "Target.h"

#include <string>

#include "Diag.h"

namespace idler
{
namespace
{
const char* FramebufferStatusName( GLenum status )
{
	switch( status )
	{
	case GL_FRAMEBUFFER_COMPLETE:                      return "complete";
	case GL_FRAMEBUFFER_UNDEFINED:                     return "undefined";
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:         return "incomplete attachment";
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "missing attachment";
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:        return "incomplete draw buffer";
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:        return "incomplete read buffer";
	case GL_FRAMEBUFFER_UNSUPPORTED:                   return "unsupported";
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:        return "incomplete multisample";
	default:                                           return "unknown";
	}
}
} // namespace

Target::~Target()
{
	// Nothing here. A destructor cannot delete GL objects safely: it may run
	// with no context current, or with a different one, and glDeleteTextures
	// against the wrong context deletes a name that belongs to somebody else.
	// DeInitGL calls Release() while the host's context is still current, and
	// that is the only correct place for it.
}

bool Target::Ensure( int newWidth, int newHeight )
{
	if( newWidth <= 0 || newHeight <= 0 )
		return false;

	if( framebuffer != 0 && newWidth == width && newHeight == height )
		return true;

	// Save what we are about to disturb. This is the whole reason this class
	// exists rather than a subclass of FFGLFBO: the SDK's version allocates
	// under a ScopedTextureBinding whose destructor clears the binding to zero
	// instead of restoring it, so allocating quietly unbinds the caller's input
	// texture -- on the allocating frame only, which is the hardest kind of bug
	// to catch.
	GLint previousTexture     = 0;
	GLint previousFramebuffer = 0;
	glGetIntegerv( GL_TEXTURE_BINDING_2D, &previousTexture );
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFramebuffer );

	Release();

	width  = newWidth;
	height = newHeight;

	glGenTextures( 1, &colourTexture );
	glBindTexture( GL_TEXTURE_2D, colourTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	// Clamp to edge, not repeat. The composite pass samples this at exactly the
	// frame edge, and a repeat wrap there takes half its filter weight from the
	// opposite side of the picture -- a one-pixel band of the wrong content down
	// two edges, which reads as a compositing bug rather than a wrap mode.
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	glGenRenderbuffers( 1, &depthBuffer );
	glBindRenderbuffer( GL_RENDERBUFFER, depthBuffer );
	glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height );
	glBindRenderbuffer( GL_RENDERBUFFER, 0 );

	glGenFramebuffers( 1, &framebuffer );
	glBindFramebuffer( GL_FRAMEBUFFER, framebuffer );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colourTexture, 0 );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer );

	const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );

	glBindFramebuffer( GL_FRAMEBUFFER, static_cast< GLuint >( previousFramebuffer ) );
	glBindTexture( GL_TEXTURE_2D, static_cast< GLuint >( previousTexture ) );

	if( status != GL_FRAMEBUFFER_COMPLETE )
	{
		diag::error( "framebuffer incomplete (" + std::string( FramebufferStatusName( status ) ) +
		             ") at " + std::to_string( width ) + "x" + std::to_string( height ) +
		             "; the plugin will draw nothing" );
		Release();
		return false;
	}

	return true;
}

void Target::Release()
{
	if( framebuffer != 0 )
		glDeleteFramebuffers( 1, &framebuffer );
	if( depthBuffer != 0 )
		glDeleteRenderbuffers( 1, &depthBuffer );
	// The one the SDK forgets. Its Release() tests depthBufferID twice.
	if( colourTexture != 0 )
		glDeleteTextures( 1, &colourTexture );

	framebuffer   = 0;
	depthBuffer   = 0;
	colourTexture = 0;
	width         = 0;
	height        = 0;
}

void Target::Bind() const
{
	glBindFramebuffer( GL_FRAMEBUFFER, framebuffer );
	glViewport( 0, 0, width, height );
}

} // namespace idler
