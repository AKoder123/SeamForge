#include "MainWindow.h"

#include "core/Export.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>

MainWindow::MainWindow() {
    setWindowTitle("SeamForge Reverse");
    resize(1500, 900);

    state_ = new AppState(this);
    viewport_ = new Viewport3D(state_);
    pattern_ = new PatternView(state_);

    auto* split = new QSplitter;
    split->addWidget(viewport_);
    split->addWidget(pattern_);
    split->setSizes({750, 750});
    setCentralWidget(split);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    auto* dock = new QDockWidget("Log && Validation");
    dock->setWidget(log_);
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    connect(state_, &AppState::logMessage, this,
            [this](const QString& m) { log_->appendPlainText(m); });
    connect(viewport_, &Viewport3D::statusText, statusBar(),
            [this](const QString& s) { statusBar()->showMessage(s, 15000); });
    connect(pattern_, &PatternView::statusText, statusBar(),
            [this](const QString& s) { statusBar()->showMessage(s, 8000); });
    connect(viewport_, &Viewport3D::seamSelected, this, [this](int id) {
        statusBar()->showMessage(id >= 0 ? QString("seam %1 selected (Delete to remove)").arg(id)
                                         : QString("no seam selected"));
    });

    // ---- file menu ----
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Import Mesh...", QKeySequence("Ctrl+I"), this, &MainWindow::importMesh);
    fileMenu->addAction("&Open Project...", QKeySequence::Open, this, &MainWindow::openProject);
    fileMenu->addAction("&Save Project...", QKeySequence::Save, this, &MainWindow::saveProject);
    fileMenu->addSeparator();
    fileMenu->addAction("Export &SVG...", this, &MainWindow::exportSvg);
    fileMenu->addAction("Export &DXF...", this, &MainWindow::exportDxf);
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);

    // ---- edit menu ----
    auto* editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction("&Undo", QKeySequence::Undo, state_, &AppState::undo);
    editMenu->addAction("&Redo", QKeySequence::Redo, state_, &AppState::redo);

    // ---- view menu ----
    auto* viewMenu = menuBar()->addMenu("&View");
    auto* wire = viewMenu->addAction("&Wireframe");
    wire->setCheckable(true);
    connect(wire, &QAction::toggled, viewport_, &Viewport3D::setWireframe);
    auto* colorGroup = new QActionGroup(this);
    auto addColorMode = [&](const QString& name, Viewport3D::ColorMode m, bool on) {
        auto* a = viewMenu->addAction(name);
        a->setCheckable(true);
        a->setChecked(on);
        colorGroup->addAction(a);
        connect(a, &QAction::toggled, viewport_, [this, m](bool en) {
            if (en) viewport_->setColorMode(m);
        });
    };
    addColorMode("Plain shading", Viewport3D::ColorMode::Plain, true);
    addColorMode("Curvature display", Viewport3D::ColorMode::Curvature, false);
    addColorMode("Segmentation preview", Viewport3D::ColorMode::Segmentation, false);
    viewMenu->addSeparator();
    viewMenu->addAction("Reset &Camera", viewport_, &Viewport3D::resetCamera);
    viewMenu->addSeparator();
    auto* rawToggle = viewMenu->addAction("Show &raw boundary (2D)");
    rawToggle->setCheckable(true);
    rawToggle->setChecked(true);
    connect(rawToggle, &QAction::toggled, pattern_, &PatternView::setShowRaw);

    // ---- tools ----
    auto* tb = addToolBar("Pipeline");
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto* toolGroup = new QActionGroup(this);
    auto addTool = [&](const QString& name, Viewport3D::Tool t, bool on) {
        auto* a = tb->addAction(name);
        a->setCheckable(true);
        a->setChecked(on);
        toolGroup->addAction(a);
        connect(a, &QAction::toggled, viewport_, [this, t](bool en) {
            if (en) viewport_->setTool(t);
        });
    };
    addTool("Navigate", Viewport3D::Tool::Navigate, true);
    addTool("Draw Seam", Viewport3D::Tool::DrawSeam, false);
    addTool("Select Seam", Viewport3D::Tool::SelectSeam, false);
    tb->addSeparator();

    tb->addAction("Propose Seams", this, [this] {
        QString log;
        state_->proposeSeams(&log);
    });
    tb->addAction("Segment", this, [this] {
        QString err;
        if (!state_->segment(&err))
            QMessageBox::warning(this, "Segmentation", err);
        else
            viewport_->setColorMode(Viewport3D::ColorMode::Segmentation);
    });

    tb->addWidget(new QLabel("  flattener: "));
    auto* flattenerBox = new QComboBox;
    flattenerBox->addItems({"arap", "lscm"});
    connect(flattenerBox, &QComboBox::currentTextChanged, this,
            [this](const QString& t) { state_->flattenerName = t; });
    tb->addWidget(flattenerBox);
    tb->addAction("Flatten", this, [this] {
        QString err;
        if (!state_->flatten(&err)) QMessageBox::warning(this, "Flattening", err);
    });
    tb->addSeparator();

    tb->addWidget(new QLabel("  heatmap: "));
    auto* heatBox = new QComboBox;
    heatBox->addItems({"angle", "area", "none"});
    connect(heatBox, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        pattern_->setHeatmap(t == "angle" ? PatternView::Heatmap::Angle
                             : t == "area" ? PatternView::Heatmap::Area
                                           : PatternView::Heatmap::None);
    });
    tb->addWidget(heatBox);

    statusBar()->showMessage(
        "Import a garment mesh, draw or propose seams, then Segment and Flatten. "
        "Double-click a 2D panel to relabel it.");
}

void MainWindow::openPath(const QString& path) {
    QString err;
    bool ok = path.endsWith(".sfrproj") ? state_->openProject(path, &err)
                                        : state_->importMesh(path, &err);
    if (!ok)
        log_->appendPlainText("open failed: " + err);
    else
        viewport_->resetCamera();
}

void MainWindow::importMesh() {
    QString path = QFileDialog::getOpenFileName(
        this, "Import garment mesh", {},
        "Meshes (*.obj *.gltf *.glb *.ply *.stl);;All files (*)");
    if (path.isEmpty()) return;
    QString err;
    if (!state_->importMesh(path, &err))
        QMessageBox::critical(this, "Import failed", err);
    else
        viewport_->resetCamera();
}

void MainWindow::openProject() {
    QString path = QFileDialog::getOpenFileName(this, "Open project", {},
                                                "SeamForge Reverse project (*.sfrproj)");
    if (path.isEmpty()) return;
    QString err;
    if (!state_->openProject(path, &err))
        QMessageBox::critical(this, "Open failed", err);
    else
        viewport_->resetCamera();
}

void MainWindow::saveProject() {
    if (!state_->hasMesh) {
        QMessageBox::information(this, "Save project", "Nothing to save yet.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Save project", "reconstruction.sfrproj",
                                                "SeamForge Reverse project (*.sfrproj)");
    if (path.isEmpty()) return;
    QString err;
    if (!state_->saveProject(path, &err))
        QMessageBox::critical(this, "Save failed", err);
}

void MainWindow::exportSvg() {
    if (state_->panels.empty() || state_->panels.front().UV.empty()) {
        QMessageBox::information(this, "Export SVG", "Flatten the panels first.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Export SVG", "pattern.svg",
                                                "SVG (*.svg)");
    if (path.isEmpty()) return;
    std::string err;
    if (!sf::writeSvg(path.toStdString(), state_->panels, state_->regularized,
                      state_->relations, {}, &err))
        QMessageBox::critical(this, "Export failed", QString::fromStdString(err));
    else
        log_->appendPlainText("SVG exported -> " + path);
}

void MainWindow::exportDxf() {
    if (state_->panels.empty() || state_->panels.front().UV.empty()) {
        QMessageBox::information(this, "Export DXF", "Flatten the panels first.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Export DXF", "pattern.dxf",
                                                "DXF (*.dxf)");
    if (path.isEmpty()) return;
    std::string err;
    if (!sf::writeDxf(path.toStdString(), state_->panels, state_->regularized, {}, &err))
        QMessageBox::critical(this, "Export failed", QString::fromStdString(err));
    else
        log_->appendPlainText("DXF exported -> " + path);
}
