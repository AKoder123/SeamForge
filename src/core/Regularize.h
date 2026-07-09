#pragma once
// Pattern regularisation: turns a raw (noisy, densely sampled) flattened
// boundary into a clean editable outline while keeping the raw polyline so
// the user can always revert. Corner-aware Douglas-Peucker simplification;
// near-straight runs are flagged so the UI/export can draw true lines.

#include "Mesh.h"
#include <vector>

namespace sf {

struct RegularizedLoop {
    std::vector<Vec2> raw;          // closed loop, raw flattened boundary
    std::vector<int> keptIdx;       // indices into raw kept by simplification
    std::vector<Vec2> simplified;   // = raw[keptIdx]
    std::vector<bool> isCorner;     // per kept point: sharp turning angle
    std::vector<bool> isStraight;   // per kept segment i -> i+1: nearly straight in raw
    double maxDeviation = 0;        // max raw-to-simplified distance
};

struct RegularizeOptions {
    double tolerance = 0.004;       // absolute simplification tolerance (mesh units)
    double cornerAngleDeg = 35.0;   // turning angle above which a point is a corner
    double straightDevFrac = 0.25;  // segment counts as straight if raw dev < frac*tolerance
};

RegularizedLoop regularizeLoop(const std::vector<Vec2>& closedLoop,
                               const RegularizeOptions& opt = {});

} // namespace sf
