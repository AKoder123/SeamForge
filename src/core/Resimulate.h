#pragma once
// Reconstruction validation (stage 17 prototype): equilibrium-consistency
// check of an inferred pattern against the source garment.
//
//   2D panels (UV) -> seam assembly -> constraint relaxation (PBD/XPBD)
//   -> reconstructed 3D -> comparison with the source mesh
//
// The particle system starts at the source 3D positions but is governed
// ONLY by the pattern: distance constraints use UV edge lengths as rest
// lengths, and paired seam vertices are pinned together. If the pattern is
// metrically consistent with the garment, relaxation stays near the source
// (drift ~ 0); wrong panel scales, wrong seam pairings or distorted
// flattening push the equilibrium away and the drift/seam-gap metrics grow.
//
// Honest scope note (KNOWN_LIMITATIONS): because the relaxation is
// initialised at the source shape, this validates METRIC consistency, not
// from-scratch drapability - a full drape reconstruction (collisions,
// gravity, body) is future work.

#include "Project.h"
#include <string>

namespace sf {

struct ResimOptions {
    int iterations = 400;         // Gauss-Seidel constraint passes (max)
    double convergeEps = 1e-6;    // stop when max correction < eps * bboxDiag
    int silhouetteRes = 96;       // raster resolution for silhouette IoU
    // pattern corruption for discrimination experiments: scale panel 0's UV
    double corruptPanel0Scale = 1.0;
};

struct ResimMetrics {
    bool success = false;
    std::string message;
    int iterationsRun = 0;
    // correspondence-exact drift vs source after rigid (Kabsch) alignment
    double driftRms = 0, driftMax = 0;    // metres
    double driftRmsRel = 0;               // / source bbox diagonal
    double chamfer = 0;                   // symmetric mean vertex distance, metres
    double meanNormalDeviationDeg = 0;
    double silhouetteIoU = 0;             // mean over +X/+Y/+Z views
    double meanSeamGap = 0, maxSeamGap = 0;  // residual seam opening, metres
};

// Requires flattened panels (UV) and seam relations in the project.
ResimMetrics resimulateValidate(const Project& proj, const ResimOptions& opt = {});

} // namespace sf
