// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Editor/MainWindow.h"
#include "Editor/ViewportWindow.h"
#include "Core/EngineSystems.h"
#include "Engine/SceneManager.h"
#include "Rendering/DeferredRenderer.h"
#include "Assets/Servers/TextureServer.h"

#include <QApplication>
#include <QActionGroup>
#include <QDockWidget>
#include <QToolBar>
#include <QMenuBar>
#include <QMenu>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QAction>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStackedWidget>
#include <QScrollArea>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QTimer>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeyEvent>
#include <QUrl>
#include <QEvent>
#include <vector>

static bool IsSupportedMapDropPath(const QString& path) {
    if (path.isEmpty()) return false;
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "map" || suffix == "n3w";
}

static bool HasSupportedMapDropUrls(const QMimeData* mimeData) {
    if (!mimeData || !mimeData->hasUrls()) return false;
    for (const QUrl& url : mimeData->urls()) {
        if (IsSupportedMapDropPath(url.toLocalFile())) return true;
    }
    return false;
}

static std::vector<std::string> CollectSupportedMapDropPaths(const QMimeData* mimeData) {
    std::vector<std::string> paths;
    if (!mimeData || !mimeData->hasUrls()) return paths;
    for (const QUrl& url : mimeData->urls()) {
        const QString local = url.toLocalFile();
        if (IsSupportedMapDropPath(local)) {
            paths.push_back(local.toStdString());
        }
    }
    return paths;
}


// ── helpers ───────────────────────────────────────────────────────────────
static QDoubleSpinBox* makeSpinBox(double lo, double hi, int decimals, QWidget* parent = nullptr) {
    auto* sb = new QDoubleSpinBox(parent);
    sb->setRange(lo, hi);
    sb->setDecimals(decimals);
    sb->setSingleStep(0.1);
    return sb;
}

// ── MainWindow ────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    if (auto* app = QApplication::instance()) {
        app->installEventFilter(this);
    }

    setWindowTitle("NDEVC Engine");
    resize(1600, 900);
    setAcceptDrops(true);

    // ── Main Menu ─────────────────────────────────────────────────────────
    QMenuBar* menuBar = this->menuBar();
    QMenu* fileMenu = menuBar->addMenu("File");
    
    QAction* newAction = fileMenu->addAction("New Scene");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::onNewScene);

    QAction* openAction = fileMenu->addAction("Open Scene...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenScene);

    QAction* importMapAction = fileMenu->addAction("Import Map...");
    connect(importMapAction, &QAction::triggered, this, &MainWindow::onImportMap);

    fileMenu->addSeparator();

    QAction* saveAction = fileMenu->addAction("Save Scene");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveScene);

    QAction* saveAsAction = fileMenu->addAction("Save Scene As...");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onSaveSceneAs);

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction("Exit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);

    // ── Toolbar: Work / Play ──────────────────────────────────────────────
    QToolBar* toolbar = addToolBar("Mode");
    toolbar->setMovable(false);

    auto* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);

    workAction_ = new QAction("Work", this);
    workAction_->setCheckable(true);
    workAction_->setChecked(true);
    modeGroup->addAction(workAction_);
    toolbar->addAction(workAction_);
    connect(workAction_, &QAction::triggered, this, &MainWindow::onWorkToggled);

    playAction_ = new QAction("Play", this);
    playAction_->setCheckable(true);
    playAction_->setChecked(false);
    modeGroup->addAction(playAction_);
    toolbar->addAction(playAction_);
    connect(playAction_, &QAction::triggered, this, &MainWindow::onPlayToggled);

    // ── 3D Viewport ───────────────────────────────────────────────────────
    viewport_ = new ViewportWindow();
    viewportContainer_ = QWidget::createWindowContainer(viewport_, this);
    viewportContainer_->setAcceptDrops(true);
    viewportContainer_->installEventFilter(this);
    viewportContainer_->setFocusPolicy(Qt::StrongFocus);
    viewportContainer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCentralWidget(viewportContainer_);

    // ── Hierarchy dock ────────────────────────────────────────────────────
    hierarchy_ = new QListWidget();
    connect(hierarchy_, &QListWidget::currentRowChanged,
            this, &MainWindow::onHierarchySelectionChanged);

    auto* hierarchyDock = new QDockWidget("Hierarchy", this);
    hierarchyDock->setWidget(hierarchy_);
    hierarchyDock->setMinimumWidth(200);
    addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock);

    // ── Properties dock ───────────────────────────────────────────────────
    propertiesStack_ = new QStackedWidget();

    auto* noSelLabel = new QLabel("No entity selected");
    noSelLabel->setAlignment(Qt::AlignCenter);
    propertiesStack_->addWidget(noSelLabel);   // page 0 — nothing selected

    auto* scrollArea  = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    auto* formWidget  = new QWidget();
    auto* formLayout  = new QFormLayout(formWidget);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    propName_ = new QLineEdit();
    formLayout->addRow("Name", propName_);
    connect(propName_, &QLineEdit::editingFinished, this, &MainWindow::onEntityNameEdited);

    formLayout->addRow(new QLabel("<b>Position</b>"));
    propPosX_ = makeSpinBox(-1e6, 1e6, 3); formLayout->addRow("X", propPosX_);
    propPosY_ = makeSpinBox(-1e6, 1e6, 3); formLayout->addRow("Y", propPosY_);
    propPosZ_ = makeSpinBox(-1e6, 1e6, 3); formLayout->addRow("Z", propPosZ_);
    connect(propPosX_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onTransformEdited);
    connect(propPosY_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onTransformEdited);
    connect(propPosZ_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onTransformEdited);

    formLayout->addRow(new QLabel("<b>Rotation (deg)</b>"));
    propRotX_ = makeSpinBox(-360.0, 360.0, 3); formLayout->addRow("X", propRotX_);
    propRotY_ = makeSpinBox(-360.0, 360.0, 3); formLayout->addRow("Y", propRotY_);
    propRotZ_ = makeSpinBox(-360.0, 360.0, 3); formLayout->addRow("Z", propRotZ_);
    connect(propRotX_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onTransformEdited);
    connect(propRotY_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onTransformEdited);
    connect(propRotZ_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onTransformEdited);

    formLayout->addRow(new QLabel("<b>Scale</b>"));
    propSclX_ = makeSpinBox(-1e4, 1e4, 3); propSclX_->setValue(1.0); formLayout->addRow("X", propSclX_);
    propSclY_ = makeSpinBox(-1e4, 1e4, 3); propSclY_->setValue(1.0); formLayout->addRow("Y", propSclY_);
    propSclZ_ = makeSpinBox(-1e4, 1e4, 3); propSclZ_->setValue(1.0); formLayout->addRow("Z", propSclZ_);
    connect(propSclX_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onTransformEdited);
    connect(propSclY_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onTransformEdited);
    connect(propSclZ_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onTransformEdited);

    propModel_ = new QLineEdit();
    propModel_->setReadOnly(true);
    formLayout->addRow("Model", propModel_);

    scrollArea->setWidget(formWidget);
    propertiesStack_->addWidget(scrollArea);   // page 1 — entity selected

    auto* propDock = new QDockWidget("Properties", this);
    propDock->setWidget(propertiesStack_);
    propDock->setMinimumWidth(250);
    addDockWidget(Qt::RightDockWidgetArea, propDock);

    // ── Assets dock ───────────────────────────────────────────────────────
    assets_ = new QTreeWidget();
    assets_->setHeaderLabel("Assets");
    assets_->setColumnCount(1);

    auto* assetsDock = new QDockWidget("Assets", this);
    assetsDock->setWidget(assets_);
    assetsDock->setMinimumHeight(180);
    addDockWidget(Qt::BottomDockWidgetArea, assetsDock);

    // ── Wire engine ready signal ──────────────────────────────────────────
    connect(viewport_, &ViewportWindow::engineReady, this, &MainWindow::onEngineReady);
}

MainWindow::~MainWindow() {
    if (auto* app = QApplication::instance()) {
        app->removeEventFilter(this);
    }
}

// ── Engine lifecycle ──────────────────────────────────────────────────────
void MainWindow::onEngineReady() {
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::onRefreshTick);
    refreshTimer_->start(1000);
}

void MainWindow::onRefreshTick() {
    refreshHierarchy();
    refreshAssets();
    refreshProperties();
    refreshTitle();
}

// ── Hierarchy ─────────────────────────────────────────────────────────────
void MainWindow::refreshHierarchy() {
    auto* engine = viewport_->GetEngine();
    if (!engine) return;
    const auto& entities = engine->GetScene().GetAuthoredEntities();

    hierarchy_->setUpdatesEnabled(false);
    hierarchy_->blockSignals(true);
    hierarchy_->clear();
    for (const auto& e : entities) {
        auto* item = new QListWidgetItem(QString::fromStdString(e.name));
        item->setData(Qt::UserRole, static_cast<qulonglong>(e.id));
        hierarchy_->addItem(item);
    }
    for (int i = 0; i < hierarchy_->count(); ++i) {
        if (hierarchy_->item(i)->data(Qt::UserRole).toULongLong() == selectedId_) {
            hierarchy_->setCurrentRow(i);
            break;
        }
    }
    hierarchy_->blockSignals(false);
    hierarchy_->setUpdatesEnabled(true);
}

void MainWindow::onHierarchySelectionChanged() {
    QListWidgetItem* item = hierarchy_->currentItem();
    if (!item) {
        selectedId_ = 0;
        propertiesStack_->setCurrentIndex(0);
        return;
    }
    selectedId_ = item->data(Qt::UserRole).toULongLong();
    refreshProperties();
}

// ── Properties ────────────────────────────────────────────────────────────
void MainWindow::refreshProperties() {
    if (selectedId_ == 0) {
        propertiesStack_->setCurrentIndex(0);
        return;
    }
    auto* engine = viewport_->GetEngine();
    if (!engine) return;

    const SceneManager::AuthoredEntity* ent = nullptr;
    for (const auto& e : engine->GetScene().GetAuthoredEntities()) {
        if (e.id == selectedId_) { ent = &e; break; }
    }
    if (!ent) {
        selectedId_ = 0;
        propertiesStack_->setCurrentIndex(0);
        return;
    }

    QWidget* fw = QApplication::focusWidget();
    auto propertiesFocused = [&]() {
        for (QWidget* p = fw; p; p = p->parentWidget()) {
            if (p == propertiesStack_) return true;
        }
        return false;
    };
    if (propertiesFocused()) return;

    propertiesStack_->setCurrentIndex(1);

    QSignalBlocker bN(propName_);
    QSignalBlocker bPX(propPosX_), bPY(propPosY_), bPZ(propPosZ_);
    QSignalBlocker bRX(propRotX_), bRY(propRotY_), bRZ(propRotZ_);
    QSignalBlocker bSX(propSclX_), bSY(propSclY_), bSZ(propSclZ_);
    QSignalBlocker bM(propModel_);

    propName_->setText(QString::fromStdString(ent->name));
    propPosX_->setValue(static_cast<double>(ent->position.x));
    propPosY_->setValue(static_cast<double>(ent->position.y));
    propPosZ_->setValue(static_cast<double>(ent->position.z));

    const glm::vec3 euler = glm::degrees(glm::eulerAngles(ent->rotation));
    propRotX_->setValue(static_cast<double>(euler.x));
    propRotY_->setValue(static_cast<double>(euler.y));
    propRotZ_->setValue(static_cast<double>(euler.z));

    propSclX_->setValue(static_cast<double>(ent->scale.x));
    propSclY_->setValue(static_cast<double>(ent->scale.y));
    propSclZ_->setValue(static_cast<double>(ent->scale.z));

    propModel_->setText(QString::fromStdString(ent->modelPath));
}

void MainWindow::onEntityNameEdited() {
    if (selectedId_ == 0) return;
    auto* engine = viewport_->GetEngine();
    if (!engine) return;
    engine->GetScene().SetEntityName(selectedId_, propName_->text().toStdString());
}

void MainWindow::onTransformEdited() {
    if (selectedId_ == 0) return;
    auto* engine = viewport_->GetEngine();
    if (!engine) return;
    const glm::vec3 pos(
        static_cast<float>(propPosX_->value()),
        static_cast<float>(propPosY_->value()),
        static_cast<float>(propPosZ_->value()));
    const glm::quat rot = glm::quat(glm::radians(glm::vec3(
        static_cast<float>(propRotX_->value()),
        static_cast<float>(propRotY_->value()),
        static_cast<float>(propRotZ_->value()))));
    const glm::vec3 scl(
        static_cast<float>(propSclX_->value()),
        static_cast<float>(propSclY_->value()),
        static_cast<float>(propSclZ_->value()));
    engine->GetScene().SetEntityTransform(selectedId_, pos, rot, scl);
}

// ── Assets ────────────────────────────────────────────────────────────────
void MainWindow::refreshAssets() {
    auto* engine = viewport_->GetEngine();
    if (!engine) return;

    assets_->setUpdatesEnabled(false);
    assets_->blockSignals(true);
    assets_->clear();

    auto* modelsRoot = new QTreeWidgetItem(assets_, QStringList("Models"));
    for (const auto& [path, refCount] : engine->GetScene().GetLoadedModelRefCounts()) {
        const QString label = QString("%1  [x%2]")
            .arg(QString::fromStdString(path))
            .arg(refCount);
        new QTreeWidgetItem(modelsRoot, QStringList(label));
    }
    modelsRoot->setExpanded(true);

    auto* texRoot = new QTreeWidgetItem(assets_, QStringList("Textures"));
    for (const auto& id : TextureServer::instance().GetLoadedTextureIds()) {
        new QTreeWidgetItem(texRoot, QStringList(QString::fromStdString(id)));
    }
    texRoot->setExpanded(false);

    assets_->blockSignals(false);
    assets_->setUpdatesEnabled(true);
}

// ── Play / Work mode ──────────────────────────────────────────────────────
void MainWindow::onPlayToggled() {
    if (playMode_) return;
    playMode_ = true;
    viewport_->SetPlayMode(true);
}

void MainWindow::onWorkToggled() {
    if (!playMode_) return;
    playMode_ = false;
    viewport_->SetPlayMode(false);
}

// ── Scene Management ──────────────────────────────────────────────────────
void MainWindow::onNewScene() {
    auto* engine = viewport_->GetEngine();
    if (!engine) return;

    if (engine->GetScene().IsSceneDirty()) {
        auto res = QMessageBox::question(this, "New Scene", 
            "The current scene has unsaved changes. Do you want to discard them?",
            QMessageBox::Yes | QMessageBox::No);
        if (res != QMessageBox::Yes) return;
    }

    engine->GetScene().CreateScene("Untitled Scene");
    selectedId_ = 0;
    refreshHierarchy();
    refreshProperties();
    refreshTitle();
}

void MainWindow::onOpenScene() {
    auto* engine = viewport_->GetEngine();
    if (!engine) return;

    if (engine->GetScene().IsSceneDirty()) {
        auto res = QMessageBox::question(this, "Open Scene", 
            "The current scene has unsaved changes. Do you want to discard them?",
            QMessageBox::Yes | QMessageBox::No);
        if (res != QMessageBox::Yes) return;
    }

    QString path = QFileDialog::getOpenFileName(this, "Open Scene", "", "NDEVC Scene (*.ndscene);;NDEVC Map (*.n3w *.map)");
    if (path.isEmpty()) return;

    if (path.endsWith(".ndscene")) {
        if (engine->GetScene().OpenScene(path.toStdString())) {
            selectedId_ = 0;
            refreshHierarchy();
            refreshProperties();
            refreshTitle();
        } else {
            QMessageBox::critical(this, "Error", "Failed to open scene: " + path);
        }
    } else {
        // Import Map
        MapLoader loader;
        auto map = loader.load_map(path.toStdString());
        if (map) {
            engine->GetScene().ImportMapAsEditableScene(map.get(), path.toStdString());
            selectedId_ = 0;
            refreshHierarchy();
            refreshProperties();
            refreshTitle();
        } else {
            QMessageBox::critical(this, "Error", "Failed to load map: " + path);
        }
    }
}

void MainWindow::onSaveScene() {
    auto* engine = viewport_->GetEngine();
    if (!engine) return;

    if (!engine->GetScene().HasActiveScenePath()) {
        onSaveSceneAs();
        return;
    }

    if (engine->GetScene().SaveScene()) {
        refreshTitle();
    } else {
        QMessageBox::critical(this, "Error", "Failed to save scene.");
    }
}

void MainWindow::onSaveSceneAs() {
    auto* engine = viewport_->GetEngine();
    if (!engine) return;

    QString path = QFileDialog::getSaveFileName(this, "Save Scene As", "", "NDEVC Scene (*.ndscene)");
    if (path.isEmpty()) return;

    if (engine->GetScene().SaveScene(path.toStdString())) {
        refreshTitle();
    } else {
        QMessageBox::critical(this, "Error", "Failed to save scene to: " + path);
    }
}

void MainWindow::onImportMap() {
    auto* engine = viewport_->GetEngine();
    if (!engine) return;

    QString path = QFileDialog::getOpenFileName(this, "Import Map", "", "NDEVC Map (*.map *.n3w)");
    if (path.isEmpty()) return;

    auto* renderer = static_cast<DeferredRenderer*>(engine->GetRenderer());
    if (renderer) {
        renderer->QueueDroppedPaths({path.toStdString()});
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == viewportContainer_) {
        if (event->type() == QEvent::MouseButtonPress) {
            viewportContainer_->setFocus(Qt::MouseFocusReason);
            viewportContainer_->grabKeyboard();
            if (viewport_) viewport_->ActivateInputCapture();
        } else if (event->type() == QEvent::FocusOut ||
                   event->type() == QEvent::WindowDeactivate) {
            viewportContainer_->releaseKeyboard();
            if (viewport_) viewport_->ReleaseInputCapture();
        }
    }

    if (event->type() == QEvent::KeyPress ||
        event->type() == QEvent::KeyRelease ||
        event->type() == QEvent::ShortcutOverride) {
        QWidget* focus = QApplication::focusWidget();
        const bool editingText =
            qobject_cast<QLineEdit*>(focus) != nullptr ||
            qobject_cast<QDoubleSpinBox*>(focus) != nullptr;
        const bool pressed = event->type() != QEvent::KeyRelease;
        if (!editingText && viewport_ && viewport_->ForwardKeyEvent(
                static_cast<QKeyEvent*>(event), pressed)) {
            return true;
        }
    }

    if (watched == viewportContainer_) {
        if (event->type() == QEvent::MouseButtonPress) {
            return false;
        } else if (event->type() == QEvent::DragEnter) {
            auto* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (HasSupportedMapDropUrls(dragEvent->mimeData())) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::DragMove) {
            auto* dragEvent = static_cast<QDragMoveEvent*>(event);
            if (HasSupportedMapDropUrls(dragEvent->mimeData())) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            auto* engine = viewport_ ? viewport_->GetEngine() : nullptr;
            auto* renderer = engine ? static_cast<DeferredRenderer*>(engine->GetRenderer()) : nullptr;
            std::vector<std::string> paths = CollectSupportedMapDropPaths(dropEvent->mimeData());
            if (renderer && !paths.empty()) {
                renderer->QueueDroppedPaths(paths);
                dropEvent->acceptProposedAction();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (HasSupportedMapDropUrls(event->mimeData())) event->acceptProposedAction();
    else event->ignore();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (HasSupportedMapDropUrls(event->mimeData())) event->acceptProposedAction();
    else event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event) {
    auto* engine = viewport_ ? viewport_->GetEngine() : nullptr;
    auto* renderer = engine ? static_cast<DeferredRenderer*>(engine->GetRenderer()) : nullptr;
    if (!renderer || !HasSupportedMapDropUrls(event->mimeData())) {
        event->ignore();
        return;
    }

    std::vector<std::string> paths = CollectSupportedMapDropPaths(event->mimeData());

    if (paths.empty()) {
        event->ignore();
        return;
    }

    renderer->QueueDroppedPaths(paths);
    event->acceptProposedAction();
}

void MainWindow::refreshTitle() {
    auto* engine = viewport_->GetEngine();
    if (!engine) {
        setWindowTitle("NDEVC Engine");
        return;
    }

    auto info = engine->GetScene().GetActiveSceneInfo();
    QString title = QString("NDEVC Engine - %1%2")
        .arg(QString::fromStdString(info.name))
        .arg(info.dirty ? "*" : "");
    setWindowTitle(title);
}

