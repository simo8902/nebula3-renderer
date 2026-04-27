// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Editor/ViewportWindow.h"

#include <QWheelEvent>
#include <QCloseEvent>
#include <QMimeData>
#include <QCursor>
#include <QUrl>
#include <QFileInfo>
#include <QTimer>
#include <vector>
#include <string>
#include <unordered_set>
#include <chrono>
#include <cstdint>
#include <cstdlib>

#include "GLFW/glfw3.h"
#include "Core/EngineSystems.h"
#include "Rendering/DeferredRenderer.h"
#include <QSurfaceFormat>
#include <QOpenGLContext>

#include "Platform/IWindow.h"
#include "Platform/IPlatform.h"
#include "Platform/NDEVcHeaders.h"
#include "Input/IInputSystem.h"

static int ToGlfwKey(const int qtKey) {
    switch (qtKey) {
    case Qt::Key_W: return GLFW_KEY_W;
    case Qt::Key_A: return GLFW_KEY_A;
    case Qt::Key_S: return GLFW_KEY_S;
    case Qt::Key_D: return GLFW_KEY_D;
    case Qt::Key_Q: return GLFW_KEY_Q;
    case Qt::Key_E: return GLFW_KEY_E;
    case Qt::Key_Space: return GLFW_KEY_SPACE;
    case Qt::Key_Control: return GLFW_KEY_LEFT_CONTROL;
    case Qt::Key_Shift: return GLFW_KEY_LEFT_SHIFT;
    case Qt::Key_F1: return GLFW_KEY_F1;
    default: return GLFW_KEY_UNKNOWN;
    }
}

static int ToGlfwMouseButton(const Qt::MouseButton button) {
    switch (button) {
    case Qt::LeftButton: return GLFW_MOUSE_BUTTON_LEFT;
    case Qt::RightButton: return GLFW_MOUSE_BUTTON_RIGHT;
    case Qt::MiddleButton: return GLFW_MOUSE_BUTTON_MIDDLE;
    default: return -1;
    }
}

static bool HasSupportedDropUrls(const QMimeData* mimeData) {
    if (!mimeData || !mimeData->hasUrls()) {
        return false;
    }
    for (const QUrl& url : mimeData->urls()) {
        const QString local = url.toLocalFile();
        if (local.isEmpty()) continue;
        const QString suffix = QFileInfo(local).suffix().toLower();
        if (suffix == "map" || suffix == "n3w") {
            return true;
        }
    }
    return false;
}

// ── GLAD proc-address loader via Qt ──────────────────────────────────────
// QOpenGLContext::getProcAddress handles both extensions (wglGetProcAddress)
// and core 1.0/1.1 symbols (GetProcAddress on opengl32) internally.
static void* qtGladLoader(const char* name) {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return nullptr;
    return reinterpret_cast<void*>(ctx->getProcAddress(name));
}

#include <mutex>

// ── QtWindow — IWindow backed by a ViewportWindow ────────────────────────
class QtWindow final : public NDEVC::Platform::IWindow {
public:
    explicit QtWindow(ViewportWindow* w) : win_(w) {}

    void MakeCurrent() const override {}   // Qt owns the context
    void ReleaseContext() const override {} // Qt owns the context
    void SwapBuffers() override {}         // QOpenGLWindow auto-swaps on paintGL return
    void PollEvents() override {
        std::lock_guard<std::mutex> lock(inputMutex_);
        activeKeys_ = pressedKeys_;
        activeMouseButtons_ = pressedMouseButtons_;
        activeX_ = lastX_;
        activeY_ = lastY_;
    }

    bool ShouldClose() const override { return shouldClose_; }
    void SetShouldClose(bool v) override  { shouldClose_ = v; }

    void GetFramebufferSize(int& w, int& h) const override {
        const qreal dpr = win_->devicePixelRatio();
        w = static_cast<int>(win_->width()  * dpr);
        h = static_cast<int>(win_->height() * dpr);
    }

    void SetFramebufferSizeCallback(std::function<void(int, int)> cb) override {
        resizeCb_ = std::move(cb);
    }
    void FireResize(int w, int h) { if (resizeCb_) resizeCb_(w, h); }

    void SetScrollCallback(std::function<void(double, double)> cb) override {
        scrollCb_ = std::move(cb);
    }
    void FireScroll(double dx, double dy) { if (scrollCb_) scrollCb_(dx, dy); }

    void* GetNativeHandle() const override { return nullptr; }
    unsigned int GetDefaultFramebuffer() const override { return win_->defaultFramebufferObject(); }

    int GetKey(int key) const override {
        return IsKeyPressed(key) ? GLFW_PRESS : GLFW_RELEASE;
    }
    bool IsKeyPressed(int key) const override {
        return activeKeys_.find(key) != activeKeys_.end();
    }
    bool IsMouseButtonPressed(int button) const override {
        return activeMouseButtons_.find(button) != activeMouseButtons_.end();
    }

    void SetKeyPressed(int key, bool pressed) {
        std::lock_guard<std::mutex> lock(inputMutex_);
        if (pressed) {
            pressedKeys_.insert(key);
        } else {
            pressedKeys_.erase(key);
        }
    }
    void SetMouseButtonPressed(int button, bool pressed) {
        std::lock_guard<std::mutex> lock(inputMutex_);
        if (pressed) {
            pressedMouseButtons_.insert(button);
        } else {
            pressedMouseButtons_.erase(button);
        }
    }
    void ClearInputState() {
        std::lock_guard<std::mutex> lock(inputMutex_);
        pressedKeys_.clear();
        pressedMouseButtons_.clear();
    }

    void GetCursorPos(double& x, double& y) const override {
        x = activeX_;
        y = activeY_;
    }
    void SetCursorPos(double x, double y) override {
        std::lock_guard<std::mutex> lock(inputMutex_);
        lastX_ = x;
        lastY_ = y;
        QCursor::setPos(win_->mapToGlobal(
            QPoint(static_cast<int>(x), static_cast<int>(y))));
    }
    void SetInputMode(int, int) override {}
    void SetSwapInterval(int interval) override {
        if (win_) {
            QSurfaceFormat fmt = win_->format();
            fmt.setSwapInterval(interval);
            win_->setFormat(fmt);
        }
    }

    void UpdateCursorPos(double x, double y) {
        std::lock_guard<std::mutex> lock(inputMutex_);
        lastX_ = x;
        lastY_ = y;
    }

private:
    ViewportWindow* win_;
    bool shouldClose_ = false;
    std::unordered_set<int> pressedKeys_;
    std::unordered_set<int> pressedMouseButtons_;
    std::function<void(int, int)>       resizeCb_;
    std::function<void(double, double)> scrollCb_;
    double lastX_ = 0.0;
    double lastY_ = 0.0;
    double activeX_ = 0.0;
    double activeY_ = 0.0;
    std::unordered_set<int> activeKeys_;
    std::unordered_set<int> activeMouseButtons_;
    mutable std::mutex inputMutex_;
};

class QtInputSystem final : public NDEVC::Platform::IInputSystem {
public:
    explicit QtInputSystem(std::shared_ptr<QtWindow> window)
        : window_(std::move(window)) {}

    bool IsKeyPressed(int key) const override {
        return window_ && window_->IsKeyPressed(key);
    }

    void GetCursorPos(double& x, double& y) const override {
        if (window_) {
            window_->GetCursorPos(x, y);
            return;
        }
        x = 0.0;
        y = 0.0;
    }

    void SetCursorPos(double x, double y) override {
        if (window_) {
            window_->SetCursorPos(x, y);
        }
    }

    float GetScrollDelta() override {
        return 0.0f;
    }

    void ResetScrollDelta() override {}

    void Update() override {}

private:
    std::shared_ptr<QtWindow> window_;
};

// ── QtPlatform — IPlatform stub ───────────────────────────────────────────
class QtPlatform final : public NDEVC::Platform::IPlatform {
public:
    explicit QtPlatform(std::shared_ptr<QtWindow> w) : window_(std::move(w)) {}

    bool Initialize() override { return true; }
    void Shutdown()   override {}

    std::shared_ptr<NDEVC::Platform::IWindow> CreateApplicationWindow(
            const std::string&, int, int) override {
        return window_;
    }
    std::shared_ptr<NDEVC::Platform::IInputSystem> CreateInputSystem() override {
        return std::make_shared<QtInputSystem>(window_);
    }
    const char* GetPlatformName() const override { return "Qt6"; }

private:
    std::shared_ptr<QtWindow> window_;
};

// ── ViewportWindow ────────────────────────────────────────────────────────
namespace {
constexpr int kViewportTargetFps = 60;
constexpr int kViewportFrameIntervalMs = 1000 / kViewportTargetFps;
}

ViewportWindow::ViewportWindow(QWindow* parent)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent) {
    QSurfaceFormat fmt;
    fmt.setVersion(4, 6);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(0);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setSwapInterval(1);
    setFormat(fmt);
    setFlag(Qt::WindowDoesNotAcceptFocus, false);
    resize(1280, 800);

    viewportClock_.start();
    lastViewportTimerNs_ = viewportClock_.nsecsElapsed();
    lastPaintStartNs_ = lastViewportTimerNs_;

    frameTimer_ = new QTimer(this);
    frameTimer_->setTimerType(Qt::PreciseTimer);
    connect(frameTimer_, &QTimer::timeout, this, [this]() {
        const qint64 nowNs = viewportClock_.nsecsElapsed();
        const double timerGapMs = static_cast<double>(nowNs - lastViewportTimerNs_) / 1000000.0;
        lastViewportTimerNs_ = nowNs;
        ++viewportTimerSeq_;

        if (paintPending_ && timerGapMs > 25.0) {
            NC::LOGGING::Warning(NC::LOGGING::Category::Editor,
                "[FRAME_TRACE][QT_TIMER_DELAY] seq=", viewportTimerSeq_,
                " timerGapMs=", timerGapMs,
                " pendingPaint=1",
                " msSinceLastPaint=", static_cast<double>(nowNs - lastPaintStartNs_) / 1000000.0);
        }

        paintPending_ = true;
        update();
    });
    frameTimer_->start(kViewportFrameIntervalMs);
    NC::LOGGING::Info(NC::LOGGING::Category::Editor,
        "[VIEWPORT] targetFps=", kViewportTargetFps,
        " timerIntervalMs=", kViewportFrameIntervalMs,
        " swapInterval=1");
}

ViewportWindow::~ViewportWindow() = default;

void ViewportWindow::initializeGL() {
    auto qtWin = std::make_shared<QtWindow>(this);
    qtWindow_  = qtWin.get();

    DeferredRenderer::SetPendingExternalPlatform(
        std::make_unique<QtPlatform>(qtWin),
        qtWin,
        qtGladLoader
    );

    NDEVC::Engine::EngineSystems::Config cfg;
    engine_ = std::make_unique<NDEVC::Engine::EngineSystems>(cfg);
    if (auto* dr = static_cast<DeferredRenderer*>(engine_->GetRenderer())) {
        dr->SetEditorHosted(true);
    }
    emit engineReady();
}

void ViewportWindow::paintGL() {
    if (!engine_) return;
    try {
        static const bool cameraTrace = []() {
            const char* value = std::getenv("NDEVC_CAMERA_TRACE");
            return value == nullptr || value[0] != '0';
        }();
        const qint64 paintStartNs = viewportClock_.nsecsElapsed();
        const double timeSinceLastPaint = static_cast<double>(paintStartNs - lastPaintStartNs_) / 1000000.0;
        const double timeSinceLastTimer = static_cast<double>(paintStartNs - lastViewportTimerNs_) / 1000000.0;
        ++paintSeq_;
        paintPending_ = false;

        auto frameLoopStart = std::chrono::high_resolution_clock::now();
        engine_->RunOneFrame();
        auto frameLoopEnd = std::chrono::high_resolution_clock::now();
        double frameLoopMs = std::chrono::duration<double, std::milli>(frameLoopEnd - frameLoopStart).count();

        if (cameraTrace && (paintSeq_ % 60 == 0 || timeSinceLastPaint > 100.0 || frameLoopMs > 40.0)) {
            NC::LOGGING::Info(NC::LOGGING::Category::Editor,
                "[CAMERA_TRACE][QT_PAINT] seq=", paintSeq_,
                " gapMs=", timeSinceLastPaint,
                " frameLoopMs=", frameLoopMs,
                " msSinceTimer=", timeSinceLastTimer,
                " timerSeq=", viewportTimerSeq_,
                " size=", width(), "x", height(),
                " dpr=", devicePixelRatio());
        }

        if (timeSinceLastPaint > 100.0) {
            NC::LOGGING::Warning("[QT] paintGL gap: ", timeSinceLastPaint, "ms (frame loop: ", frameLoopMs, "ms)");
        }

        lastPaintStartNs_ = paintStartNs;
    } catch (const std::exception& e) {
        NC::LOGGING::Error("[QT] paintGL exception: ", e.what());
        throw;
    } catch (...) {
        NC::LOGGING::Error("[QT] paintGL unknown exception");
        throw;
    }
}

void ViewportWindow::resizeGL(int w, int h) {
    if (!qtWindow_) return;
    const qreal dpr = devicePixelRatio();
    qtWindow_->FireResize(static_cast<int>(w * dpr),
                          static_cast<int>(h * dpr));
}

void ViewportWindow::wheelEvent(QWheelEvent* event) {
    if (qtWindow_) {
        const double delta = event->angleDelta().y() / 120.0;
        qtWindow_->FireScroll(0.0, delta);
    }
    event->accept();
}

void ViewportWindow::ActivateInputCapture() {
    requestActivate();
    setKeyboardGrabEnabled(true);
}

void ViewportWindow::ReleaseInputCapture() {
    setKeyboardGrabEnabled(false);
    if (qtWindow_) {
        qtWindow_->ClearInputState();
    }
}

void ViewportWindow::keyPressEvent(QKeyEvent* event) {
    if (ForwardKeyEvent(event, true)) return;
    QOpenGLWindow::keyPressEvent(event);
}

void ViewportWindow::keyReleaseEvent(QKeyEvent* event) {
    if (ForwardKeyEvent(event, false)) return;
    QOpenGLWindow::keyReleaseEvent(event);
}

bool ViewportWindow::ForwardKeyEvent(QKeyEvent* event, bool pressed) {
    if (!qtWindow_ || !event) {
        return false;
    }
    
    // Engineering Mandate: Properly handle Qt auto-repeat.
    // We consume repeat events to prevent them from propagating to the UI,
    // but we ONLY change the internal 'pressed' state on physical events.
    if (event->isAutoRepeat()) {
        return true; 
    }

    const int key = ToGlfwKey(event->key());
    if (key == GLFW_KEY_UNKNOWN) {
        return false;
    }
    qtWindow_->SetKeyPressed(key, pressed);
    event->accept();
    return true;
}

void ViewportWindow::mousePressEvent(QMouseEvent* event) {
    ActivateInputCapture();
    if (qtWindow_) {
        const int button = ToGlfwMouseButton(event->button());
        if (button >= 0) qtWindow_->SetMouseButtonPressed(button, true);
        
        // Update stored cursor position immediately on press
        const QPointF p = event->position();
        qtWindow_->UpdateCursorPos(p.x(), p.y());
    }
    event->accept();
}

void ViewportWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (qtWindow_) {
        const int button = ToGlfwMouseButton(event->button());
        if (button >= 0) qtWindow_->SetMouseButtonPressed(button, false);
    }
    event->accept();
}

void ViewportWindow::mouseMoveEvent(QMouseEvent* event) {
    if (qtWindow_) {
        const QPointF p = event->position();
        qtWindow_->UpdateCursorPos(p.x(), p.y());
    }
    event->accept();
}

void ViewportWindow::focusInEvent(QFocusEvent* event) {
    if (qtWindow_) {
        // Start clean when gaining focus
        qtWindow_->ClearInputState();
    }
    QOpenGLWindow::focusInEvent(event);
}

void ViewportWindow::focusOutEvent(QFocusEvent* event) {
    ReleaseInputCapture();
    QOpenGLWindow::focusOutEvent(event);
}

void ViewportWindow::SetPlayMode(bool play) {
    if (!engine_) return;
    auto* dr = static_cast<DeferredRenderer*>(engine_->GetRenderer());
    if (!dr) return;
    dr->SetRenderMode(play ? RenderMode::Play : RenderMode::Work);
}

void ViewportWindow::closeEvent(QCloseEvent* event) {
    if (engine_)   engine_->RequestStop();
    ReleaseInputCapture();
    if (qtWindow_) qtWindow_->SetShouldClose(true);
    QOpenGLWindow::closeEvent(event);
}

void ViewportWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (HasSupportedDropUrls(event->mimeData())) event->acceptProposedAction();
    else event->ignore();
}

void ViewportWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (HasSupportedDropUrls(event->mimeData())) event->acceptProposedAction();
    else event->ignore();
}

void ViewportWindow::dropEvent(QDropEvent* event) {
    if (!engine_) return;
    auto* renderer = static_cast<DeferredRenderer*>(engine_->GetRenderer());
    if (!renderer) return;

    std::vector<std::string> paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        QString local = url.toLocalFile();
        if (!local.isEmpty()) {
            const QString suffix = QFileInfo(local).suffix().toLower();
            if (suffix == "map" || suffix == "n3w") {
                paths.push_back(local.toStdString());
            }
        }
    }

    if (!paths.empty()) {
        renderer->QueueDroppedPaths(paths);
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}