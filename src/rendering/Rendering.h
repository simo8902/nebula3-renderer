// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_RENDERING_H
#define NDEVC_RENDERING_H

#include "Rendering/Camera.h"
#include "Rendering/DrawCmd.h"
#include <memory>

struct MapData;

namespace NDEVC::Graphics {
class IRenderer;
}

void SetGLErrorChecking(bool enabled);

extern Camera camera;

class Rendering {
	std::unique_ptr<NDEVC::Graphics::IRenderer> impl_;

public:
	static std::unique_ptr<NDEVC::Graphics::IRenderer> CreateDefaultRenderer();

	Rendering();
	~Rendering();

	void initGLFW();
	void initCascadedShadowMaps();
	void initLOOP();
	void resizeFramebuffers(int newWidth, int newHeight);

	void SetCheckGLErrors(bool enabled);
	bool GetCheckGLErrors() const;
};
#endif
