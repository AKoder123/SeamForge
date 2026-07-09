#pragma once

#include "AppState.h"
#include "PatternView.h"
#include "Viewport3D.h"

#include <QMainWindow>
#include <QPlainTextEdit>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void openPath(const QString& path);   // mesh or .sfrproj (used by CLI flags)

private slots:
    void importMesh();
    void openProject();
    void saveProject();
    void exportSvg();
    void exportDxf();

private:
    AppState* state_;
    Viewport3D* viewport_;
    PatternView* pattern_;
    QPlainTextEdit* log_;
};
