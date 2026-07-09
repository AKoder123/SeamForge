#pragma once
// Pattern regularisation: turns a raw (noisy, densely sampled) flattened
// boundary into a clean editable outline while keeping the raw polyline so
// the user can always revert. Corner-aware Douglas-Peucker simplification;
// near-straight runs are flagged so the UI/export can draw true lines.

#include "Mesh.h"
#include <vector>

namespace sf {

struct BezierSegment {
    Vec2 p0, c0, c1, p1;            // cubic control points (line: c at 1/3, 2/3)
    bool isLine = false;
};

struct RegularizedLoop {
    std::vector<Vec2> raw;          // closed loop, raw flattened boundary
    std::vector<int> keptIdx;       // indices into raw kept by simplification
    std::vector<Vec2> simplified;   // = raw[keptIdx]
    std::vector<bool> isCorner;     // per kept point: sharp turning angle
    std::vector<bool> isStraight;   // per kept segment i -> i+1: nearly straight in raw
    double maxDeviation = 0;        // max raw-to-simplified distance

    // Smooth outline: ordered chain of cubic Bezier/line segments around the
    // loop (filled by fitLoopCurves; empty until then). Consecutive segments
    // share endpoints; the chain closes back to curves.front().p0.
    std::vector<BezierSegment> curves;
    double curveMaxDeviation = 0;   // fitted-curve vs raw distance bound
    double curveMaxLengthError = 0; // worst per-span relative arc-length error
    bool hasCurves() const { return !curves.empty(); }
};

struct RegularizeOptions {
    double tolerance = 0.004;       // absolute simplification tolerance (mesh units)
    double cornerAngleDeg = 35.0;   // turning angle above which a point is a corner
    double straightDevFrac = 0.25;  // segment counts as straight if raw dev < frac*tolerance
};

RegularizedLoop regularizeLoop(const std::vector<Vec2>& closedLoop,
                               const RegularizeOptions& opt = {});

struct CurveFitOptions {
    double tolerance = 0.004;        // max fitted-curve deviation (mesh units)
    double lineDevFrac = 0.35;       // span is a straight line if raw dev < frac*tol
    double maxLengthError = 0.005;   // relative arc-length error budget per span
                                     // (seam-length compatibility); fitting
                                     // subdivides further until met
};

// Fits smooth cubic Bezier curves (Schneider least-squares fitting with
// adaptive splitting) to the raw boundary between detected corners, filling
// reg.curves / curveMaxDeviation / curveMaxLengthError. Straight spans become
// true line segments; corners are preserved exactly (C0 at corners, smooth
// inside spans). The raw polyline is untouched, so the fit is revertible.
void fitLoopCurves(RegularizedLoop& reg, const CurveFitOptions& opt = {});

// Arc length of a fitted chain (line-sampled cubics).
double curveChainLength(const std::vector<BezierSegment>& curves);

// Evaluate a cubic segment at t in [0,1].
Vec2 bezierPoint(const BezierSegment& b, double t);

} // namespace sf
