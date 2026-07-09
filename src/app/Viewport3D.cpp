#include "Viewport3D.h"

#include <GL/gl.h>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {

// column-major 4x4 helpers (avoid GLU dependency)
void identity(double* m) {
    for (int i = 0; i < 16; ++i) m[i] = i % 5 == 0 ? 1.0 : 0.0;
}
void mul(const double* a, const double* b, double* out) {   // out = a*b
    double r[16];
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            r[c * 4 + rr] = 0;
            for (int k = 0; k < 4; ++k) r[c * 4 + rr] += a[k * 4 + rr] * b[c * 4 + k];
        }
    std::copy(r, r + 16, out);
}
bool transformPoint(const double* m, const double* p, double* out) {   // 4-vec
    for (int i = 0; i < 4; ++i)
        out[i] = m[0 * 4 + i] * p[0] + m[1 * 4 + i] * p[1] + m[2 * 4 + i] * p[2] +
                 m[3 * 4 + i] * p[3];
    return std::abs(out[3]) > 1e-12;
}

QColor curvatureColor(double g, double scale) {
    // blue (negative) - white (flat) - red (positive)
    double t = std::clamp(g / scale, -1.0, 1.0);
    if (t >= 0) return QColor(255, int(255 * (1 - t)), int(255 * (1 - t)));
    return QColor(int(255 * (1 + t)), int(255 * (1 + t)), 255);
}

const QColor kPanelColors[] = {
    QColor(120, 170, 220), QColor(230, 170, 120), QColor(150, 210, 150),
    QColor(210, 150, 210), QColor(220, 210, 130), QColor(140, 210, 210),
};

} // namespace

Viewport3D::Viewport3D(AppState* state, QWidget* parent)
    : QOpenGLWidget(parent), state_(state) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    connect(state_, &AppState::changed, this, &Viewport3D::refresh);
}

void Viewport3D::setTool(Tool t) {
    tool_ = t;
    if (t != Tool::DrawSeam) {
        pendingAnchors_.clear();
        pendingPath_.clear();
    }
    if (t == Tool::DrawSeam)
        emit statusText("Draw seam: click vertices along the surface; the path snaps to "
                        "shortest edge paths. Enter = commit, Backspace = remove last, Esc = cancel.");
    else if (t == Tool::SelectSeam)
        emit statusText("Select seam: click a seam curve. Delete removes the selected seam.");
    else
        emit statusText("");
    update();
}

void Viewport3D::refresh() {
    if (state_->hasMesh && dist_ == 2.5 && target_.isZero()) resetCamera();
    // drop stale references
    if (selectedSeam_ >= 0) {
        bool found = false;
        for (const auto& s : state_->seams) found |= s.id == selectedSeam_;
        if (!found) selectedSeam_ = -1;
    }
    for (int v : pendingAnchors_)
        if (v >= (int)state_->mesh.V.size()) {
            pendingAnchors_.clear();
            pendingPath_.clear();
            break;
        }
    update();
}

void Viewport3D::resetCamera() {
    if (!state_->hasMesh) return;
    auto bb = state_->mesh.bbox();
    target_ = 0.5 * (bb.min() + bb.max());
    dist_ = 2.2 * bb.diagonal().norm();
    update();
}

void Viewport3D::initializeGL() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.16f, 0.17f, 0.19f, 1.0f);
}

void Viewport3D::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!state_->hasMesh) return;

    const double aspect = width() > 0 ? double(width()) / std::max(1, height()) : 1.0;
    const double fovY = 40.0 * M_PI / 180.0, zn = 0.01 * dist_, zf = 50.0 * dist_;
    const double f = 1.0 / std::tan(fovY / 2);
    identity(proj_);
    proj_[0] = f / aspect;
    proj_[5] = f;
    proj_[10] = (zf + zn) / (zn - zf);
    proj_[11] = -1;
    proj_[14] = 2 * zf * zn / (zn - zf);
    proj_[15] = 0;

    // camera: orbit around target
    sf::Vec3 eyeDir(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
                    std::cos(pitch_) * std::cos(yaw_));
    sf::Vec3 eye = target_ + dist_ * eyeDir;
    sf::Vec3 up(0, 1, 0);
    sf::Vec3 zax = (eye - target_).normalized();
    sf::Vec3 xax = up.cross(zax).normalized();
    sf::Vec3 yax = zax.cross(xax);
    identity(mv_);
    mv_[0] = xax.x(); mv_[4] = xax.y(); mv_[8] = xax.z();
    mv_[1] = yax.x(); mv_[5] = yax.y(); mv_[9] = yax.z();
    mv_[2] = zax.x(); mv_[6] = zax.y(); mv_[10] = zax.z();
    mv_[12] = -xax.dot(eye);
    mv_[13] = -yax.dot(eye);
    mv_[14] = -zax.dot(eye);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixd(proj_);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixd(mv_);
    viewport_[0] = 0; viewport_[1] = 0;
    viewport_[2] = width(); viewport_[3] = height();

    drawMesh();
    drawSeams();
    drawPending();
}

void Viewport3D::drawMesh() {
    const auto& m = state_->mesh;
    sf::Vec3 eyeDir(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
                    std::cos(pitch_) * std::cos(yaw_));

    // face -> panel id for segmentation preview
    std::vector<int> facePanel;
    if (colorMode_ == ColorMode::Segmentation && !state_->panels.empty()) {
        facePanel.assign(m.F.size(), -1);
        for (const auto& p : state_->panels)
            for (int f : p.faces)
                if (f < (int)facePanel.size()) facePanel[f] = p.id;
    }
    double curvScale = 0;
    if (colorMode_ == ColorMode::Curvature) {
        std::vector<double> mags;
        for (double g : state_->curvature.gaussian) mags.push_back(std::abs(g));
        std::sort(mags.begin(), mags.end());
        curvScale = mags.empty() ? 1.0 : std::max(1e-9, mags[mags.size() * 9 / 10]);
    }

    glBegin(GL_TRIANGLES);
    for (int f = 0; f < (int)m.F.size(); ++f) {
        sf::Vec3 n = m.faceNormal(f);
        double lit = 0.35 + 0.65 * std::abs(n.dot(eyeDir));
        QColor base(200, 200, 205);
        if (colorMode_ == ColorMode::Segmentation && !facePanel.empty() && facePanel[f] >= 0)
            base = kPanelColors[facePanel[f] % 6];
        for (int k = 0; k < 3; ++k) {
            int v = m.F[f][k];
            QColor c = base;
            if (colorMode_ == ColorMode::Curvature)
                c = curvatureColor(state_->curvature.gaussian[v], curvScale);
            glColor3d(c.redF() * lit, c.greenF() * lit, c.blueF() * lit);
            glVertex3d(m.V[v].x(), m.V[v].y(), m.V[v].z());
        }
    }
    glEnd();

    if (wireframe_) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.0f, -1.0f);
        glColor3d(0.25, 0.28, 0.32);
        glBegin(GL_TRIANGLES);
        for (const auto& t : m.F)
            for (int k = 0; k < 3; ++k)
                glVertex3d(m.V[t[k]].x(), m.V[t[k]].y(), m.V[t[k]].z());
        glEnd();
        glDisable(GL_POLYGON_OFFSET_LINE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

void Viewport3D::drawSeams() {
    const auto& m = state_->mesh;
    glDisable(GL_DEPTH_TEST);
    for (const auto& s : state_->seams) {
        QColor c;
        if (s.id == selectedSeam_) c = QColor(255, 255, 80);
        else if (s.source == sf::Seam::Source::Proposed) {
            // confidence overlay: red (low) -> green (high)
            double t = std::clamp(s.confidence, 0.0, 1.0);
            c = QColor(int(255 * (1 - t)), int(200 * t + 55), 60);
        } else c = QColor(240, 80, 80);
        glLineWidth(s.id == selectedSeam_ ? 5.0f : 3.0f);
        glColor3d(c.redF(), c.greenF(), c.blueF());
        glBegin(GL_LINE_STRIP);
        for (int v : s.vertices) glVertex3d(m.V[v].x(), m.V[v].y(), m.V[v].z());
        glEnd();
    }
    glEnable(GL_DEPTH_TEST);
}

void Viewport3D::drawPending() {
    if (pendingPath_.empty() && pendingAnchors_.empty()) return;
    const auto& m = state_->mesh;
    glDisable(GL_DEPTH_TEST);
    glColor3d(0.4, 0.85, 1.0);
    glLineWidth(3.0f);
    glBegin(GL_LINE_STRIP);
    for (int v : pendingPath_) glVertex3d(m.V[v].x(), m.V[v].y(), m.V[v].z());
    glEnd();
    glPointSize(8.0f);
    glBegin(GL_POINTS);
    for (int v : pendingAnchors_) glVertex3d(m.V[v].x(), m.V[v].y(), m.V[v].z());
    glEnd();
    glEnable(GL_DEPTH_TEST);
}

bool Viewport3D::project(const sf::Vec3& p, double* winX, double* winY, double* winZ) const {
    double mvp[16];
    mul(proj_, mv_, mvp);
    double in[4] = {p.x(), p.y(), p.z(), 1.0}, out[4];
    if (!transformPoint(mvp, in, out)) return false;
    double ndcX = out[0] / out[3], ndcY = out[1] / out[3], ndcZ = out[2] / out[3];
    *winX = viewport_[0] + (ndcX + 1) * 0.5 * viewport_[2];
    *winY = viewport_[1] + (1 - ndcY) * 0.5 * viewport_[3];   // Qt y-down
    *winZ = ndcZ;
    return out[3] > 0;
}

std::optional<int> Viewport3D::pickVertex(const QPoint& pos) {
    // screen-space nearest vertex with a modest depth preference: robust,
    // resolution-independent, and cheap at our mesh sizes
    const auto& m = state_->mesh;
    double bestD = 18.0 * 18.0;   // pixel radius
    double bestZ = 2.0;
    int best = -1;
    for (int v = 0; v < (int)m.V.size(); ++v) {
        double x, y, z;
        if (!project(m.V[v], &x, &y, &z)) continue;
        double dx = x - pos.x(), dy = y - pos.y();
        double d = dx * dx + dy * dy;
        if (d < bestD && z < bestZ + 0.05) {
            bestD = d;
            bestZ = std::min(bestZ, z);
            best = v;
        }
    }
    if (best < 0) return std::nullopt;
    return best;
}

int Viewport3D::pickSeam(const QPoint& pos) {
    const auto& m = state_->mesh;
    double bestD = 12.0 * 12.0;
    int best = -1;
    for (const auto& s : state_->seams)
        for (int v : s.vertices) {
            double x, y, z;
            if (!project(m.V[v], &x, &y, &z)) continue;
            double dx = x - pos.x(), dy = y - pos.y();
            if (dx * dx + dy * dy < bestD) {
                bestD = dx * dx + dy * dy;
                best = s.id;
            }
        }
    return best;
}

void Viewport3D::rebuildPendingPath() {
    pendingPath_.clear();
    for (size_t i = 0; i + 1 < pendingAnchors_.size(); ++i) {
        auto seg = sf::shortestVertexPath(state_->mesh, pendingAnchors_[i],
                                          pendingAnchors_[i + 1]);
        if (seg.empty()) continue;
        if (!pendingPath_.empty()) seg.erase(seg.begin());
        pendingPath_.insert(pendingPath_.end(), seg.begin(), seg.end());
    }
    if (pendingPath_.empty() && pendingAnchors_.size() == 1)
        pendingPath_ = pendingAnchors_;
}

void Viewport3D::commitPending() {
    if (pendingPath_.size() < 2) {
        emit statusText("seam needs at least two connected vertices");
        return;
    }
    int id = state_->addSeam(pendingPath_, sf::Seam::Source::Manual, 1.0);
    pendingAnchors_.clear();
    pendingPath_.clear();
    if (id >= 0) {
        emit statusText(QString("seam %1 committed").arg(id));
        emit seamCommitted();
    }
    update();
}

void Viewport3D::mousePressEvent(QMouseEvent* e) {
    lastMouse_ = e->pos();
    if (tool_ == Tool::DrawSeam && e->button() == Qt::LeftButton && state_->hasMesh) {
        if (auto v = pickVertex(e->pos())) {
            pendingAnchors_.push_back(*v);
            rebuildPendingPath();
            update();
        }
        return;
    }
    if (tool_ == Tool::SelectSeam && e->button() == Qt::LeftButton && state_->hasMesh) {
        selectedSeam_ = pickSeam(e->pos());
        emit seamSelected(selectedSeam_);
        update();
        return;
    }
}

void Viewport3D::mouseMoveEvent(QMouseEvent* e) {
    QPoint d = e->pos() - lastMouse_;
    lastMouse_ = e->pos();
    if (e->buttons() & Qt::LeftButton && tool_ == Tool::Navigate) {
        yaw_ -= d.x() * 0.01;
        pitch_ = std::clamp(pitch_ + d.y() * 0.01, -1.5, 1.5);
        update();
    } else if (e->buttons() & (Qt::MiddleButton | Qt::RightButton)) {
        // pan in view plane
        sf::Vec3 eyeDir(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
                        std::cos(pitch_) * std::cos(yaw_));
        sf::Vec3 up(0, 1, 0);
        sf::Vec3 zax = eyeDir;
        sf::Vec3 xax = up.cross(zax).normalized();
        sf::Vec3 yax = zax.cross(xax);
        double scale = dist_ * 0.0015;
        target_ += (-d.x() * scale) * xax + (d.y() * scale) * yax;
        update();
    }
}

void Viewport3D::wheelEvent(QWheelEvent* e) {
    dist_ *= std::pow(0.999, e->angleDelta().y());
    dist_ = std::clamp(dist_, 1e-4, 1e6);
    update();
}

void Viewport3D::keyPressEvent(QKeyEvent* e) {
    if (tool_ == Tool::DrawSeam) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            commitPending();
            return;
        }
        if (e->key() == Qt::Key_Backspace) {
            if (!pendingAnchors_.empty()) pendingAnchors_.pop_back();
            rebuildPendingPath();
            update();
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            pendingAnchors_.clear();
            pendingPath_.clear();
            update();
            return;
        }
    }
    if (tool_ == Tool::SelectSeam && e->key() == Qt::Key_Delete && selectedSeam_ >= 0) {
        state_->deleteSeam(selectedSeam_);
        selectedSeam_ = -1;
        return;
    }
    QOpenGLWidget::keyPressEvent(e);
}
