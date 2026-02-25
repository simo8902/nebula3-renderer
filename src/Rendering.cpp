// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering.h"
#include "DeferredRenderer.h"

Camera camera = Camera(
	"MainCamera",
	glm::vec3(0.0f, 0.0f, 3.0f),
	glm::vec3(0.0f, 0.0f, -1.0f),
	glm::vec3(0.0f, 1.0f, 0.0f),
	0.0f, 0.0f, 35.0f, 0.1f, 45.0f, 0.1f, 1000.0f
);

bool gEnableGLErrorChecking = false;

void SetGLErrorChecking(bool enabled) {
	gEnableGLErrorChecking = enabled;
}

Rendering::Rendering() : impl_(std::make_unique<DeferredRenderer>()) {
}

Rendering::~Rendering() = default;

void Rendering::initGLFW() {
	impl_->Initialize();
}

void Rendering::initDeferred() {
}

void Rendering::initCascadedShadowMaps() {
}

void Rendering::initLOOP() {
	impl_->RenderFrame();
}

void Rendering::appendN3WTransform(const std::string& path, const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) {
	impl_->AppendModel(path, pos, rot, scale);
}

void Rendering::LoadMapInstances(const MapData* map) {
	impl_->LoadMap(map);
}

void Rendering::ReloadMapWithCurrentMode() {
	impl_->ReloadMapWithCurrentMode();
}

void Rendering::resizeFramebuffers(int newWidth, int newHeight) {
	impl_->Resize(newWidth, newHeight);
}

void Rendering::SetCheckGLErrors(bool enabled) {
	impl_->SetCheckGLErrors(enabled);
}

bool Rendering::GetCheckGLErrors() const {
	return impl_->GetCheckGLErrors();
}
