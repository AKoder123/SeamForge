#pragma once
// 3D garment viewport: orbit/pan/zoom camera, flat-shaded mesh with
// selectable colour modes (plain / curvature / segmentation preview /
// seam confidence), wireframe overlay, seam display, and interactive
// seam drawing (vertex picking + geodesic snapping via Dijkstra).

#include "AppState.h"

#include <QOpenGLWidget>
#include <optional>

class Viewport3D : public QOpenGLWidget {
    Q_OBJECT
public:
    enum class ColorMode { Plain, Curvature, Segmentation, Confidence };
    enum class Tool { Navigate, DrawSeam, SelectSeam };

    explicit Viewport3D(AppState* state, QWidget* parent = nullptr);

    void setColorMode(ColorMode m) { colorMode_ = m; update(); }
    void setWireframe(bool on) { wireframe_ = on; update(); }
    void setTool(Tool t);
    Tool tool() const { return tool_; }
    int selectedSeam() const { return selectedSeam_; }

signals:
    void seamCommitted();
    void statusText(const QString& s);
    void seamSelected(int seamId);

public slots:
    void refresh();          // state changed
    void resetCamera();

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void drawMesh();
    void drawSeams();
    void drawPending();
    std::optional<int> pickVertex(const QPoint& pos);   // nearest vertex under cursor
    int pickSeam(const QPoint& pos);
    bool project(const sf::Vec3& p, double* winX, double* winY, double* winZ) const;
    void commitPending();

    AppState* state_;
    ColorMode colorMode_ = ColorMode::Plain;
    Tool tool_ = Tool::Navigate;
    bool wireframe_ = false;

    // camera
    double yaw_ = 0.6, pitch_ = 0.35, dist_ = 2.5;
    sf::Vec3 target_ = sf::Vec3::Zero();
    QPoint lastMouse_;

    // seam drawing
    std::vector<int> pendingAnchors_;
    std::vector<int> pendingPath_;    // full vertex path through anchors
    void rebuildPendingPath();
    int selectedSeam_ = -1;

    // cached matrices for picking (column-major, set in paintGL)
    double mv_[16] = {}, proj_[16] = {};
    int viewport_[4] = {};
};
