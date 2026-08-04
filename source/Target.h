#pragma once

#include <FFGLSDK.h>

/**
    An off-screen colour + depth target.

    ## Why this exists at all

    Every other generator in the fleet renders straight into whatever
    framebuffer the host bound, and orrery goes out of its way to avoid
    allocating one -- because `ffglex::FFGLFBO` has two bugs, and because
    everything orrery draws can be reached with a background pass and a blend
    function.

    Idler cannot do that. Six of its savers are perspective 3D with geometry
    that overlaps itself, and **a depth test needs a depth buffer**. The host's
    framebuffer is colour-only; there is nothing to borrow. Attaching a depth
    renderbuffer to the host's own FBO would work and is not worth the
    consequences -- it mutates an object the host owns, on a frame the host did
    not expect it, and the SDK gives no promise about what else is looking at it.

    So Idler allocates. This class is that allocation, written by hand rather
    than subclassed off `FFGLFBO`, because both of the SDK's bugs live in the
    parts that would have been inherited:

    - **`FFGLFBO::Release()` leaks the colour texture.** It deletes the
      framebuffer and the depth renderbuffer, then tests `depthBufferID` a
      second time where it plainly meant `colorTextureID`. A plugin that
      reallocates on every resolution change leaks a full-frame texture each
      time, and the operator dragging a composition size around is exactly the
      case that reallocates.
    - **`FFGLFBO::Initialise` allocates under a `ScopedTextureBinding`, whose
      destructor CLEARS the binding to 0 rather than restoring it.** So
      allocating a buffer silently unbinds whatever input texture was on the
      active unit. The symptom is the dangerous part: correct on every frame
      *except* the one that allocates, so the effect variant drops its clip for
      exactly one frame after load and one frame per resize.

    `Ensure()` below saves and restores `GL_TEXTURE_BINDING_2D` and
    `GL_FRAMEBUFFER_BINDING` around the allocation for that second reason. It is
    cheap and it is the difference between a bug that shows up in testing and
    one that shows up on a stage.

    ## What it does not do

    No multisampling. The 2D savers are line drawings whose antialiasing comes
    from the geometry -- `Mesh::AddPolyline` builds a real quad, and the edge
    softness is a fragment-shader falloff -- and the 3D savers are hard-edged
    solids that read correctly aliased, as they did on the machine. A 4x MSAA
    target would cost four times the bandwidth to make 3D Pipes look less like
    3D Pipes.
*/
namespace idler
{

class Target
{
public:
	~Target();

	/// Allocate or reallocate for `width` x `height`. A no-op if the size has
	/// not changed, which is the common case -- this runs every frame.
	///
	/// Returns false if the framebuffer would not complete, having logged why.
	bool Ensure( int width, int height );

	void Release();

	/// Bind for drawing, and set the viewport to the whole target.
	void Bind() const;

	GLuint ColourTexture() const { return colourTexture; }
	int Width() const { return width; }
	int Height() const { return height; }
	bool Valid() const { return framebuffer != 0; }

private:
	GLuint framebuffer   = 0;
	GLuint colourTexture = 0;
	GLuint depthBuffer   = 0;
	int width            = 0;
	int height           = 0;
};

} // namespace idler
