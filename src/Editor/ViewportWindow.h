// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#pragma once

#include "glad/glad.h"     // must precede Qt OpenGL headers to prevent GL symbol conflict
#include <QOpenGLWindow>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QFocusEvent>
#include <QElapsedTimer>
#include <memory>

namespace NDEVC::Engine {
    class EngineSystems;
}

class QtWindow;
class QTimer;

class ViewportWindow : public QOpenGLWindow {
    Q_OBJECT
public:
    explicit ViewportWindow(QWindow* parent = nullptr);
    ~ViewportWindow() override;

    NDEVC::Engine::EngineSystems* GetEngine() const { return engine_.get(); }
    void SetPlayMode(bool play);
    void ActivateInputCapture();
    void ReleaseInputCapture();
    bool ForwardKeyEvent(QKeyEvent* event, bool pressed);

Q_SIGNALS:
    void engineReady();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event);
    void dragMoveEvent(QDragMoveEvent* event);
    void dropEvent(QDropEvent* event);

private:
    std::unique_ptr<NDEVC::Engine::EngineSystems> engine_;
    QtWindow* qtWindow_ = nullptr;
    QTimer* frameTimer_ = nullptr;
    QElapsedTimer viewportClock_;
    qint64 lastViewportTimerNs_ = 0;
    qint64 lastPaintStartNs_ = 0;
    uint64_t viewportTimerSeq_ = 0;
    uint64_t paintSeq_ = 0;
    bool paintPending_ = false;
};
