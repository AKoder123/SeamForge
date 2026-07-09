#include "PatternView.h"

#include <QGraphicsPolygonItem>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
QColor heatColor(double t01) {
    double t = std::clamp(t01, 0.0, 1.0);
    return QColor(int(240 + t * 15) > 255 ? 255 : int(240 + t * 15),
                  int(240 - t * 170), int(240 - t * 190));
}
constexpr double kScale = 1000.0;   // metres -> mm in scene units
} // namespace

ControlPointItem::ControlPointItem(PatternView* view, int panelIdx, int loopIdx,
                                   int keptIdx, double r)
    : QGraphicsEllipseItem(-r, -r, 2 * r, 2 * r),
      view_(view), panelIdx_(panelIdx), loopIdx_(loopIdx), keptIdx_(keptIdx) {
    setFlag(ItemIsMovable);
    setFlag(ItemSendsGeometryChanges);
    setBrush(QColor(30, 90, 200));
    setPen(QPen(Qt::white, 0.4));
    setZValue(10);
    setCursor(Qt::SizeAllCursor);
}

QVariant ControlPointItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionHasChanged)
        view_->controlPointMoved(panelIdx_, loopIdx_, keptIdx_, pos());
    return QGraphicsEllipseItem::itemChange(change, value);
}

PatternView::PatternView(AppState* state, QWidget* parent)
    : QGraphicsView(parent), state_(state) {
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(0.5, -0.5);   // y-up like the pattern coordinate system
    connect(state_, &AppState::changed, this, &PatternView::rebuild);
}

void PatternView::rebuild() {
    if (rebuilding_) return;
    rebuilding_ = true;
    scene()->clear();
    places_.clear();

    double xOff = 0;
    const double spacing = 40.0;   // mm
    for (size_t pi = 0; pi < state_->panels.size(); ++pi) {
        const sf::Panel& p = state_->panels[pi];
        if (p.UV.empty()) continue;

        sf::Vec2 mn(1e300, 1e300), mx(-1e300, -1e300);
        for (const auto& q : p.UV) {
            mn = mn.cwiseMin(q);
            mx = mx.cwiseMax(q);
        }
        auto mapPt = [&](const sf::Vec2& q) {
            return QPointF((q.x() - mn.x()) * kScale + xOff, (q.y() - mn.y()) * kScale);
        };
        places_.push_back({xOff, p.id, (mx - mn).x() * kScale, (mx - mn).y() * kScale});

        // distortion heatmap
        if (heatmap_ != Heatmap::None) {
            auto fd = sf::computeDistortion(p, p.UV);
            for (size_t f = 0; f < p.F.size(); ++f) {
                const auto& t = p.F[f];
                QPolygonF tri;
                tri << mapPt(p.UV[t[0]]) << mapPt(p.UV[t[1]]) << mapPt(p.UV[t[2]]);
                double v = heatmap_ == Heatmap::Angle
                               ? (fd[f].angleDistortion() - 1.0) / 0.5
                               : std::abs(std::log(std::max(fd[f].areaRatio(), 1e-9))) / 0.5;
                QColor c = fd[f].flipped ? QColor(150, 40, 200) : heatColor(v);
                auto* item = scene()->addPolygon(tri, Qt::NoPen, c);
                item->setZValue(0);
            }
        }

        // raw boundary
        sf::TriMesh pm = p.toTriMesh();
        auto loops = pm.boundaryLoops();
        if (showRaw_) {
            for (const auto& loop : loops) {
                QPolygonF poly;
                for (int v : loop) poly << mapPt(p.UV[v]);
                auto* item = scene()->addPolygon(poly, QPen(QColor(160, 160, 160), 0.5));
                item->setZValue(1);
            }
        }

        // regularised outline with editable control points; fitted Bezier
        // path drawn when available (polyline kept underneath as the
        // editable/revert layer)
        if (pi < state_->regularized.size()) {
            for (size_t li = 0; li < state_->regularized[pi].size(); ++li) {
                const auto& reg = state_->regularized[pi][li];
                if (reg.hasCurves()) {
                    QPainterPath path(mapPt(reg.curves.front().p0));
                    for (const auto& b : reg.curves) {
                        if (b.isLine)
                            path.lineTo(mapPt(b.p1));
                        else
                            path.cubicTo(mapPt(b.c0), mapPt(b.c1), mapPt(b.p1));
                    }
                    path.closeSubpath();
                    auto* curveItem = scene()->addPath(path, QPen(Qt::black, 1.2));
                    curveItem->setZValue(3);
                }
                QPolygonF poly;
                for (const auto& q : reg.simplified) poly << mapPt(q);
                auto* item = scene()->addPolygon(
                    poly, QPen(reg.hasCurves() ? QColor(90, 120, 200) : QColor(0, 0, 0),
                               reg.hasCurves() ? 0.5 : 1.2));
                item->setZValue(2);
                for (size_t k = 0; k < reg.simplified.size(); ++k) {
                    auto* cp = new ControlPointItem(this, (int)pi, (int)li, (int)k,
                                                    reg.isCorner[k] ? 3.5 : 2.5);
                    cp->setPos(mapPt(reg.simplified[k]));
                    if (reg.isCorner[k]) cp->setBrush(QColor(200, 60, 60));
                    scene()->addItem(cp);
                }
            }
        }

        // seam labels
        for (const auto& seg : p.segments) {
            if (seg.seamId < 0 || seg.localVerts.empty()) continue;
            QPointF at = mapPt(p.UV[seg.localVerts[seg.localVerts.size() / 2]]);
            auto* label = scene()->addSimpleText(QString("S%1").arg(seg.seamId));
            label->setBrush(QColor(190, 40, 40));
            label->setTransform(QTransform::fromScale(2.0, -2.0));
            label->setPos(at);
            label->setZValue(5);
        }

        // panel label + dimensions
        auto* title = scene()->addSimpleText(
            QString("panel %1 (%2)  %3 x %4 mm")
                .arg(p.id)
                .arg(QString::fromStdString(p.label))
                .arg((mx - mn).x() * 1000, 0, 'f', 0)
                .arg((mx - mn).y() * 1000, 0, 'f', 0));
        title->setBrush(QColor(220, 220, 230));
        title->setTransform(QTransform::fromScale(2.5, -2.5));
        title->setPos(xOff, -18.0);
        title->setZValue(5);

        xOff += (mx - mn).x() * kScale + spacing;
    }
    scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-40, -40, 40, 40));
    rebuilding_ = false;
}

void PatternView::controlPointMoved(int panelIdx, int loopIdx, int keptIdx,
                                    const QPointF& scenePos) {
    if (rebuilding_) return;
    if (panelIdx >= (int)state_->regularized.size()) return;
    if (loopIdx >= (int)state_->regularized[panelIdx].size()) return;
    auto& reg = state_->regularized[panelIdx][loopIdx];
    if (keptIdx >= (int)reg.simplified.size()) return;

    // scene -> panel UV coordinates
    const sf::Panel& p = state_->panels[panelIdx];
    sf::Vec2 mn(1e300, 1e300);
    for (const auto& q : p.UV) mn = mn.cwiseMin(q);
    double xOff = 0;
    for (const auto& pl : places_)
        if (pl.panelId == p.id) xOff = pl.xOff;
    reg.simplified[keptIdx] = sf::Vec2((scenePos.x() - xOff) / kScale + mn.x(),
                                       scenePos.y() / kScale + mn.y());
    emit statusText(QString("boundary point moved (panel %1); raw boundary kept for revert")
                        .arg(p.id));
}

void PatternView::wheelEvent(QWheelEvent* e) {
    double s = std::pow(1.0015, e->angleDelta().y());
    scale(s, s);
    e->accept();
}

void PatternView::mouseDoubleClickEvent(QMouseEvent* e) {
    // double-click a panel area to cycle its label
    QPointF sp = mapToScene(e->pos());
    for (const auto& pl : places_) {
        if (sp.x() >= pl.xOff && sp.x() <= pl.xOff + pl.w && sp.y() >= 0 && sp.y() <= pl.h) {
            static const char* cycle[] = {"front", "back", "sleeve", "collar", "unknown"};
            for (auto& p : state_->panels)
                if (p.id == pl.panelId) {
                    int cur = 4;
                    for (int i = 0; i < 5; ++i)
                        if (p.label == cycle[i]) cur = i;
                    state_->setPanelLabel(pl.panelId, cycle[(cur + 1) % 5]);
                    return;
                }
        }
    }
    QGraphicsView::mouseDoubleClickEvent(e);
}
