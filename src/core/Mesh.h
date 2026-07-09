#pragma once
// Triangle surface mesh with edge-based adjacency (lightweight half-edge
// equivalent). All pipeline stages operate on this structure.
//
// Design notes:
//  * Faces store CCW vertex indices. Adjacency (edges, edge->faces,
//    vertex->faces) is derived and must be rebuilt after topology changes.
//  * Edges are undirected (v0 < v1); the incident face list preserves
//    non-manifold configurations so validation can report them.

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sf {

using Vec3 = Eigen::Vector3d;
using Vec2 = Eigen::Vector2d;

struct Edge {
    int v0 = -1, v1 = -1;          // v0 < v1
    std::vector<int> faces;        // incident faces (size 1 = boundary, 2 = interior, >2 = non-manifold)
    bool boundary() const { return faces.size() == 1; }
    bool nonManifold() const { return faces.size() > 2; }
};

class TriMesh {
public:
    std::vector<Vec3> V;
    std::vector<std::array<int, 3>> F;

    // ---- derived adjacency ----
    std::vector<Edge> edges;
    std::vector<std::array<int, 3>> faceEdges;   // per face: edge index opposite of local vertex i? No: edge (i,i+1)
    std::vector<std::vector<int>> vertexFaces;
    std::vector<std::vector<int>> vertexEdges;

    void buildAdjacency();
    bool adjacencyValid() const { return adjacencyValid_; }
    void invalidateAdjacency() { adjacencyValid_ = false; }

    int edgeBetween(int a, int b) const;         // -1 if none
    int numBoundaryEdges() const;
    int numNonManifoldEdges() const;

    // Ordered boundary loops (vertex indices). Requires consistent winding;
    // on non-manifold boundaries the result is best-effort.
    std::vector<std::vector<int>> boundaryLoops() const;

    // Per-face connected-component id via shared (manifold) edges.
    // `blockedEdges` (edge indices) are treated as cuts.
    std::vector<int> faceComponents(int& count,
                                    const std::vector<char>* blockedEdges = nullptr) const;

    Eigen::AlignedBox3d bbox() const;
    Vec3 faceNormal(int f) const;                // unit; zero for degenerate
    double faceArea(int f) const;
    double totalArea() const;
    double edgeLength(int e) const { return (V[edges[e].v0] - V[edges[e].v1]).norm(); }

private:
    bool adjacencyValid_ = false;
    std::unordered_map<std::uint64_t, int> edgeMap_;   // key(a,b) -> edge index
    static std::uint64_t ekey(int a, int b) {
        if (a > b) std::swap(a, b);
        return (std::uint64_t(std::uint32_t(a)) << 32) | std::uint32_t(b);
    }
};

// Dijkstra shortest path between two vertices along mesh edges.
// `edgeCost` overrides Euclidean length when provided (must be > 0).
// Returns empty vector if unreachable.
std::vector<int> shortestVertexPath(const TriMesh& m, int from, int to,
                                    const std::vector<double>* edgeCost = nullptr);

} // namespace sf
