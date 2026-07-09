#pragma once
// 2D pattern view: extracted panels with distortion heatmaps, raw vs
// regularised boundaries, editable control points on the regularised
// outline, seam labels, dimensions and panel labels.

#include "AppState.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsView>

class PatternView : public QGraphicsView {
    Q_OBJECT
public:
    enum class Heatmap { None, Angle, Area };

    explicit PatternView(AppState* state, QWidget* parent = nullptr);
    void setHeatmap(Heatmap h) { heatmap_ = h; rebuild(); }
    void setShowRaw(bool on) { showRaw_ = on; rebuild(); }

signals:
    void statusText(const QString& s);

public slots:
    void rebuild();

protected:
    void wheelEvent(QWheelEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    AppState* state_;
    Heatmap heatmap_ = Heatmap::Angle;
    bool showRaw_ = true;
    struct PanelPlace { double xOff; int panelId; double w, h; };
    std::vector<PanelPlace> places_;
    bool rebuilding_ = false;

    friend class ControlPointItem;
    void controlPointMoved(int panelIdx, int loopIdx, int keptIdx, const QPointF& scenePos);
};

// draggable control point on a regularised boundary
class ControlPointItem : public QGraphicsEllipseItem {
public:
    ControlPointItem(PatternView* view, int panelIdx, int loopIdx, int keptIdx,
                     double r);
protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
private:
    PatternView* view_;
    int panelIdx_, loopIdx_, keptIdx_;
};
