// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Rendering/Rendering.h"
#include "Rendering/DeferredRenderer.h"
#include "Core/Logger.h"

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

void Rendering::initDeferred() {
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
