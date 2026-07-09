#pragma once
// Independent-boundary seam matching: proposes which pre-existing panel
// boundaries should be sewn together when panels arrive already cut
// (e.g. a garment supplied as separate panel meshes). Complements the
// deterministic cut-ancestry pairing in Relations.h, which only covers
// boundaries we created ourselves.
//
// Method: split each panel boundary loop at 3D corners into arcs, then
// score arc pairs across panels by length similarity and mean closest-point
// (Chamfer) distance in the original 3D placement; greedy best-first
// assignment. Every match is a proposal: confidence is capped below 1.0,
// ambiguous runners-up and unmatched arcs are reported, never hidden.

#include "Relations.h"
#include "Seam.h"
#include "Segmentation.h"

#include <string>
#include <vector>

namespace sf {

struct BoundaryMatchOptions {
    double cornerAngleDeg = 40.0;  // split boundary loops at 3D turning angles above this
    int samples = 16;              // arc-length samples per arc for distance scoring
    double maxRelDistance = 0.05;  // accept if mean Chamfer distance <= frac * arc length
    double minLengthRatio = 0.8;   // accept if shorter/longer arc length >= this
    double ambiguityFactor = 0.8;  // runner-up within this factor of the winner => flagged
    int firstSeamId = 0;           // ids assigned to created seams start here
    double maxConfidence = 0.9;    // proposals never reach user-confirmed 1.0
};

struct BoundaryMatchResult {
    // One seam per matched pair (source = MeshBoundary, geometry = side A's
    // path in original mesh indices) plus its relation
    // (source = "boundary-matching"). relations[i] pairs with seams[i].
    std::vector<Seam> seams;
    std::vector<SeamRelation> relations;
    std::vector<std::string> unmatchedArcs;   // human-readable, per arc
    std::string log;
};

// `panels` must carry exact original-mesh correspondence (toOrigV), as
// produced by segmentBySeams (with an empty seam list, disconnected
// components become one panel each).
BoundaryMatchResult proposeBoundaryMatches(const std::vector<Panel>& panels,
                                           const BoundaryMatchOptions& opt = {});

} // namespace sf
