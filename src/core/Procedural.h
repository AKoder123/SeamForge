#pragma once
// Procedural evaluation meshes with analytic ground truth (panels + seams).
// Used by the automated tests, the experiments and the benchmark dataset.

#include "Mesh.h"
#include "Seam.h"
#include <string>
#include <vector>

namespace sf {

struct GroundTruth {
    std::vector<int> faceLabel;              // 0 = front, 1 = back, ...
    std::vector<std::vector<int>> seamPaths; // ordered vertex paths (side seams)
    int panelCount = 0;
};

struct SkirtOptions {
    double waistRadius = 0.35;   // metres
    double hemRadius = 0.55;
    double height = 0.6;
    int radialSegments = 64;     // must be even; seams at angle 0 and pi
    int rings = 16;
    int panels = 2;              // 2 or 4 (seams every 2pi/panels)
    double noise = 0.0;          // relative vertex jitter (fraction of height)
    // profile bulge: 0 = straight frustum (developable); >0 = curved A-line
    double bulge = 0.0;
    unsigned seed = 42;
};

// Open tube (waist + hem boundary loops), axis +Y, seams along constant-angle
// vertex columns. Ground truth labels faces by angular sector.
TriMesh makeSkirt(const SkirtOptions& opt, GroundTruth* gt = nullptr);

} // namespace sf
