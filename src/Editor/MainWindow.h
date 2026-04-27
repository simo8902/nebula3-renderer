// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#pragma once

#include "glad/glad.h"
#include <QMainWindow>
#include <cstdint>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QObject;
class QEvent;
class ViewportWindow;
class QListWidget;
class QDoubleSpinBox;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QTimer;
class QAction;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private Q_SLOTS:
    void onEngineReady();
    void onRefreshTick();
    void onHierarchySelectionChanged();
    void onEntityNameEdited();
    void onTransformEdited();
    void onPlayToggled();
    void onWorkToggled();
    void onNewScene();
    void onOpenScene();
    void onSaveScene();
    void onSaveSceneAs();
    void onImportMap();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void refreshHierarchy();
    void refreshProperties();
    void refreshAssets();
    void refreshTitle();

    ViewportWindow*   viewport_        = nullptr;
    QListWidget*      hierarchy_       = nullptr;
    QStackedWidget*   propertiesStack_ = nullptr;
    QTreeWidget*      assets_          = nullptr;
    QTimer*           refreshTimer_    = nullptr;
    QAction*          playAction_      = nullptr;
    QAction*          workAction_      = nullptr;
    QWidget*          viewportContainer_ = nullptr;

    QLineEdit*      propName_  = nullptr;
    QDoubleSpinBox* propPosX_  = nullptr;
    QDoubleSpinBox* propPosY_  = nullptr;
    QDoubleSpinBox* propPosZ_  = nullptr;
    QDoubleSpinBox* propRotX_  = nullptr;
    QDoubleSpinBox* propRotY_  = nullptr;
    QDoubleSpinBox* propRotZ_  = nullptr;
    QDoubleSpinBox* propSclX_  = nullptr;
    QDoubleSpinBox* propSclY_  = nullptr;
    QDoubleSpinBox* propSclZ_  = nullptr;
    QLineEdit*      propModel_ = nullptr;

    uint64_t selectedId_ = 0;
    bool     playMode_   = false;
};
