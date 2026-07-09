#pragma once
// Discrete curvature features used for visualisation and seam-candidate
// scoring: per-vertex angle defect (Gaussian), per-vertex mean-curvature
// magnitude (cotan Laplacian), per-edge dihedral deviation.

#include "Mesh.h"
#include <vector>

namespace sf {

struct CurvatureField {
    std::vector<double> gaussian;      // per vertex, angle defect / mixed area
    std::vector<double> meanMagnitude; // per vertex, |Laplace(x)| / (2 * mixed area)
    std::vector<double> dihedral;      // per edge, |pi - dihedral angle| (0 = flat)
};

CurvatureField computeCurvature(const TriMesh& m);

} // namespace sf
