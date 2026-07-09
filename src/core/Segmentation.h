#pragma once
// Cuts the garment mesh along confirmed seam curves and produces individual
// panel meshes. Exact 3D-to-panel correspondence is preserved: every panel
// vertex/face records its originating mesh vertex/face. Boundary ancestry
// (which seam, or the original mesh boundary, produced each boundary edge)
// is recorded so seam correspondence stays deterministic.

#include "Mesh.h"
#include "Seam.h"
#include <string>
#include <vector>

namespace sf {

struct BoundarySegment {
    int seamId = -1;             // -1: original mesh boundary
    std::vector<int> localVerts; // ordered along the boundary loop (>= 2)
};

struct Panel {
    int id = -1;
    std::string label = "unknown";        // front / back / sleeve / collar / unknown
    std::vector<int> faces;               // original face ids (== origFace)
    // local mesh
    std::vector<Vec3> V;
    std::vector<std::array<int, 3>> F;
    std::vector<int> toOrigV;             // local vertex -> original vertex
    std::vector<int> origFace;            // local face   -> original face
    std::vector<std::vector<int>> boundaryLoopsLocal;  // ordered local vertex loops
    std::vector<BoundarySegment> segments;             // ancestry per boundary run
    // filled by flattening
    std::vector<Vec2> UV;

    TriMesh toTriMesh() const;
    double area3d() const;
};

struct SegmentationResult {
    std::vector<Panel> panels;
    std::vector<std::string> problems;    // topological issues, non-separating seams...
    bool ok() const { return problems.empty(); }
};

SegmentationResult segmentBySeams(const TriMesh& m, const std::vector<Seam>& seams);

} // namespace sf
