#pragma once
// Seam correspondence: which panel boundaries are sewn together.
//
// For boundaries created by cutting one seam curve, pairing is recovered
// deterministically from cut ancestry (confidence 1.0). Both sides list
// their local vertices in the ORIGINAL seam-path order, so the sewing
// correspondence is vertex-exact. `reversed` expresses the user-facing
// sewing direction and can be toggled; unequal lengths and partial matches
// are reported, not hidden.

#include "Flatten.h"
#include "Segmentation.h"
#include <optional>
#include <string>
#include <vector>

namespace sf {

struct SeamSide {
    int panelId = -1;
    std::vector<int> localVerts;   // ordered along the original seam path
    double length2d(const Panel& p) const;
};

struct SeamRelation {
    int seamId = -1;
    SeamSide a, b;
    bool reversed = false;          // user-editable sewing direction flag
    double confidence = 1.0;
    std::string source = "cut-ancestry";
    double length3d = 0;
    double lengthMismatch2d = 0;    // |lenA - lenB| / max(lenA, lenB) after flattening
    std::string note;               // ambiguity / partial-match reporting
};

// Derives relations for every seam that separates two panels. Seams bounded
// by the same panel on both sides, or matched only partially, are reported
// in `note` with reduced confidence.
std::vector<SeamRelation> deriveSeamRelations(const TriMesh& mesh,
                                              const std::vector<Seam>& seams,
                                              const SegmentationResult& seg);

// Recomputes 2D length mismatch after flattening (panels must carry UV).
void updateRelationLengths(std::vector<SeamRelation>& rels,
                           const std::vector<Panel>& panels);

} // namespace sf
