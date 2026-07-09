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
    std::vector<std::vector<int>> seamPaths; // ordered vertex paths (panel-separating)
    std::vector<std::vector<int>> dartPaths; // partial interior seams (dart creases)
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
    // waist darts (radius pinch creases descending from the waist); they do
    // NOT change the panel count - dart paths land in GroundTruth::dartPaths
    int darts = 0;             // 0 or 2 (placed on the front half)
    double dartDepth = 0.10;   // max relative radius pinch at the crease
    double dartLength = 0.45;  // fraction of height the crease descends
    double dartWidth = 0.5;    // angular half-width of the pinch (radians)
};

// Open tube (waist + hem boundary loops), axis +Y, seams along constant-angle
// vertex columns. Ground truth labels faces by angular sector.
TriMesh makeSkirt(const SkirtOptions& opt, GroundTruth* gt = nullptr);

// ---- two-sheet sewn garments -----------------------------------------------
// Front and back panels are bulged height-field grids over a silhouette
// region, sharing boundary vertices exactly where sewn; openings (neck,
// waist, cuffs, ankles) are un-shared outline runs. Seams, face labels and
// topology are analytic by construction.

struct BoxyTeeOptions {
    double cell = 0.02;            // grid cell size (metres)
    double torsoHalfWidth = 0.28;
    double height = 0.72;          // waist (y=0) to shoulder line
    double sleeveHalfSpan = 0.62;  // cuff-to-cuff half width
    double armpitY = 0.46;         // sleeve underside height
    double neckHalfWidth = 0.10;
    double depth = 0.10;           // max half-thickness of the bulge
    double taperMargin = 0.07;     // distance over which the bulge closes at seams
};
// Kimono/boxy tee sewn from 2 cross-shaped panels. Openings: neck, waist,
// two cuffs (4 boundary loops). Seams: overarm+shoulder (x2), side+underarm (x2).
TriMesh makeBoxyTee(const BoxyTeeOptions& opt, GroundTruth* gt = nullptr);

struct FlatTrousersOptions {
    double cell = 0.02;
    double hipHalfWidth = 0.26;
    double height = 1.0;           // ankle (y=0) to waist
    double crotchY = 0.55;
    double legInnerGap = 0.05;     // half-gap between the legs
    double depth = 0.09;
    double taperMargin = 0.06;
};
// Pyjama-style trousers sewn from 2 silhouette panels. Openings: waist, two
// ankles (3 boundary loops). Seams: two outseams, one inseam+crotch path.
TriMesh makeFlatTrousers(const FlatTrousersOptions& opt, GroundTruth* gt = nullptr);

} // namespace sf
