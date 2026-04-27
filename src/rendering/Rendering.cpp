// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/Rendering.h"
#include "Rendering/DeferredRenderer.h"
#include "Core/Logger.h"

Camera camera = Camera(
	"MainCamera",
	glm::vec3(0.0f, 2.0f, 10.0f),       // positioned slightly up and back
	glm::vec3(0.0f, 0.0f, -1.0f),       // looking forward (-Z)
	glm::vec3(0.0f, 1.0f, 0.0f),        // world up
	270.0f, 0.0f,                       // yaw, pitch
	8.0f, 0.1f,                         // move speed, mouse sensitivity
	60.0f, 0.1f, 10000.0f               // fov, near, far
);

bool gEnableGLErrorChecking = false;

void SetGLErrorChecking(bool enabled) {
	gEnableGLErrorChecking = enabled;
	NC::LOGGING::Log("[RENDERING] SetGLErrorChecking ", (enabled ? 1 : 0));
}

std::unique_ptr<NDEVC::Graphics::IRenderer> Rendering::CreateDefaultRenderer() {
	NC::LOGGING::Log("[RENDERING] CreateDefaultRenderer");
	return std::make_unique<DeferredRenderer>();
}

Rendering::Rendering() : impl_(CreateDefaultRenderer()) {
	NC::LOGGING::Log("[RENDERING] Rendering ctor");
}

Rendering::~Rendering() {
	NC::LOGGING::Log("[RENDERING] Rendering dtor");
}

void Rendering::initGLFW() {
	NC::LOGGING::Log("[RENDERING] initGLFW begin");
	impl_->Initialize();
	NC::LOGGING::Log("[RENDERING] initGLFW end");
}

void Rendering::initCascadedShadowMaps() {
}

void Rendering::initLOOP() {
	NC::LOGGING::Log("[RENDERING] initLOOP tick");
	impl_->RenderFrame();
}

void Rendering::resizeFramebuffers(int newWidth, int newHeight) {
	NC::LOGGING::Log("[RENDERING] resizeFramebuffers w=", newWidth, " h=", newHeight);
	impl_->Resize(newWidth, newHeight);
}

void Rendering::SetCheckGLErrors(bool enabled) {
	NC::LOGGING::Log("[RENDERING] SetCheckGLErrors ", (enabled ? 1 : 0));
	impl_->SetCheckGLErrors(enabled);
}

bool Rendering::GetCheckGLErrors() const {
	return impl_->GetCheckGLErrors();
}
