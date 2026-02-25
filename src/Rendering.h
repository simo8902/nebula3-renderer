// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_RENDERING_H
#define NDEVC_RENDERING_H

#include "Camera.h"
#include "DrawCmd.h"
#include <memory>

class DeferredRenderer;
struct MapData;

void SetGLErrorChecking(bool enabled);

extern Camera camera;

class Rendering {
	std::unique_ptr<DeferredRenderer> impl_;

public:
	Rendering();
	~Rendering();

	void initGLFW();
	void initDeferred();
	void initCascadedShadowMaps();
	void initLOOP();
	void appendN3WTransform(const std::string& path, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale);
	void LoadMapInstances(const MapData* map);
	void ReloadMapWithCurrentMode();
	void resizeFramebuffers(int newWidth, int newHeight);

	void SetCheckGLErrors(bool enabled);
	bool GetCheckGLErrors() const;
};
#endif