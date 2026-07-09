#pragma once
// Automatic segmentation baselines (no user seams). Currently: a
// D-Charts-style quasi-developable chart growing baseline after
// Julius, Kraevoy, Sheffer, "D-Charts: Quasi-Developable Mesh
// Segmentation" (Eurographics 2005), reimplemented from the paper.
//
// Idea: a developable chart's face normals lie on a circle of the Gauss
// sphere, i.e. fit a cone (axis N, opening angle theta). Charts are grown
// greedily by fitting error with Lloyd-style proxy re-fitting, and must
// stay topological disks (a disk chart is flattenable; enforcing this
// forces >= 2 charts on tube-like garments).
//
// Known, documented behaviour on garments (see RESEARCH.md §2.3): chart
// boundaries minimise distortion, not construction convention — on a
// smooth skirt the wrap-around cut lands at an arbitrary meridian, so
// IoU against true construction panels is structurally limited. This
// baseline exists to quantify exactly that gap.

#include "Mesh.h"
#include <string>
#include <vector>

namespace sf {

struct DChartsOptions {
    int maxCharts = 16;        // hard cap; growth adds charts only when needed
    int iterations = 10;       // Lloyd re-fit + regrow passes
    double distanceWeight = 0.5;  // compactness: cost += w * dist(face, seed)/bboxDiag
};

struct DChartsResult {
    std::vector<int> faceChart;      // per face: chart id (>= 0, all faces covered)
    int chartCount = 0;
    std::vector<double> chartRmsFit; // per chart: rms cone-fitting error (radian-ish)
    int iterationsRun = 0;
    std::string log;
};

DChartsResult dchartsSegment(const TriMesh& m, const DChartsOptions& opt = {});

} // namespace sf
