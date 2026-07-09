#pragma once
// Seam curves: ordered vertex paths along mesh edges. Seams may come from
// manual drawing, an automatic proposer, or the original mesh boundary.
// Every candidate carries provenance and a confidence score so the UI can
// expose uncertainty instead of hiding it.

#include "Mesh.h"
#include <string>
#include <vector>

namespace sf {

struct Seam {
    int id = -1;
    std::vector<int> vertices;   // >= 2; consecutive entries share a mesh edge
    enum class Source { Manual, Proposed, MeshBoundary };
    Source source = Source::Manual;
    double confidence = 1.0;     // 1.0 for user-confirmed
    std::string evidence;        // human-readable supporting evidence

    bool closed() const {
        return vertices.size() > 2 && vertices.front() == vertices.back();
    }
    double length3d(const TriMesh& m) const;
};

// Verifies a seam path: consecutive vertices connected by edges, no
// (non-endpoint) repeats. Returns empty string when valid.
std::string validateSeamPath(const TriMesh& m, const Seam& s);

// Edge indices covered by the seam path.
std::vector<int> seamEdgeIndices(const TriMesh& m, const Seam& s);

// --- assisted proposal (experiment 3) -------------------------------------
// Proposes the two side seams of a skirt-like garment: vertical geodesic
// paths connecting the two largest boundary loops through the silhouette
// extremes of the widest horizontal axis. Confidence combines curvature
// support along the path with the strength of the silhouette prior.
struct SeamProposalResult {
    std::vector<Seam> seams;      // ranked, best first
    std::string log;
};
SeamProposalResult proposeSideSeams(const TriMesh& m);

} // namespace sf
