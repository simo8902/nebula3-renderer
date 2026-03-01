#ifndef NDEVC_GLHANDLES_H
#define NDEVC_GLHANDLES_H

#include "Rendering/OpenGL/GLHandles.h"

namespace NDEVC::Graphics::GL {

using UniqueTexture = NDEVC::GL::GLTexHandle;
using UniqueFramebuffer = NDEVC::GL::GLFBOHandle;
using UniqueBuffer = NDEVC::GL::GLBufHandle;
using UniqueVertexArray = NDEVC::GL::GLVAOHandle;
using UniqueProgram = NDEVC::GL::GLProgHandle;

}

#endif
