#include "Mesh.h"

#include <algorithm>
#include <limits>
#include <queue>

namespace sf {

void TriMesh::buildAdjacency() {
    edges.clear();
    faceEdges.assign(F.size(), {-1, -1, -1});
    vertexFaces.assign(V.size(), {});
    vertexEdges.assign(V.size(), {});
    edgeMap_.clear();
    edgeMap_.reserve(F.size() * 3 / 2 + 8);

    for (int f = 0; f < (int)F.size(); ++f) {
        const auto& t = F[f];
        for (int k = 0; k < 3; ++k) {
            int a = t[k], b = t[(k + 1) % 3];
            auto key = ekey(a, b);
            auto it = edgeMap_.find(key);
            int e;
            if (it == edgeMap_.end()) {
                e = (int)edges.size();
                Edge ed;
                ed.v0 = std::min(a, b);
                ed.v1 = std::max(a, b);
                edges.push_back(std::move(ed));
                edgeMap_.emplace(key, e);
                vertexEdges[edges[e].v0].push_back(e);
                vertexEdges[edges[e].v1].push_back(e);
            } else {
                e = it->second;
            }
            edges[e].faces.push_back(f);
            faceEdges[f][k] = e;
            vertexFaces[t[k]].push_back(f);
        }
    }
    adjacencyValid_ = true;
}

int TriMesh::edgeBetween(int a, int b) const {
    auto it = edgeMap_.find(ekey(a, b));
    return it == edgeMap_.end() ? -1 : it->second;
}

int TriMesh::numBoundaryEdges() const {
    int n = 0;
    for (const auto& e : edges) n += e.boundary() ? 1 : 0;
    return n;
}

int TriMesh::numNonManifoldEdges() const {
    int n = 0;
    for (const auto& e : edges) n += e.nonManifold() ? 1 : 0;
    return n;
}

std::vector<std::vector<int>> TriMesh::boundaryLoops() const {
    // Directed boundary half-edges: face is CCW, so if the directed edge
    // (a,b) appears in a face and the edge has one incident face, the
    // boundary travels (b,a). Chain succ[b] = a.
    std::unordered_map<int, int> succ;
    for (int f = 0; f < (int)F.size(); ++f) {
        for (int k = 0; k < 3; ++k) {
            int a = F[f][k], b = F[f][(k + 1) % 3];
            int e = edgeBetween(a, b);
            if (e >= 0 && edges[e].boundary()) succ[b] = a;
        }
    }
    std::vector<std::vector<int>> loops;
    std::unordered_map<int, char> used;
    for (const auto& [start, next] : succ) {
        if (used.count(start)) continue;
        std::vector<int> loop;
        int v = start;
        while (!used.count(v)) {
            used[v] = 1;
            loop.push_back(v);
            auto it = succ.find(v);
            if (it == succ.end()) break;   // open chain (non-manifold boundary)
            v = it->second;
        }
        if (loop.size() >= 3) loops.push_back(std::move(loop));
    }
    std::sort(loops.begin(), loops.end(),
              [](const auto& a, const auto& b) { return a.size() > b.size(); });
    return loops;
}

std::vector<int> TriMesh::faceComponents(int& count,
                                         const std::vector<char>* blockedEdges) const {
    std::vector<int> comp(F.size(), -1);
    count = 0;
    std::vector<int> stack;
    for (int f0 = 0; f0 < (int)F.size(); ++f0) {
        if (comp[f0] >= 0) continue;
        stack.push_back(f0);
        comp[f0] = count;
        while (!stack.empty()) {
            int f = stack.back();
            stack.pop_back();
            for (int k = 0; k < 3; ++k) {
                int e = faceEdges[f][k];
                if (blockedEdges && (*blockedEdges)[e]) continue;
                if (edges[e].faces.size() != 2) continue;   // do not cross non-manifold fans
                for (int g : edges[e].faces) {
                    if (comp[g] < 0) {
                        comp[g] = count;
                        stack.push_back(g);
                    }
                }
            }
        }
        ++count;
    }
    return comp;
}

Eigen::AlignedBox3d TriMesh::bbox() const {
    Eigen::AlignedBox3d b;
    for (const auto& p : V) b.extend(p);
    return b;
}

Vec3 TriMesh::faceNormal(int f) const {
    const auto& t = F[f];
    Vec3 n = (V[t[1]] - V[t[0]]).cross(V[t[2]] - V[t[0]]);
    double l = n.norm();
    return l > 1e-20 ? Vec3(n / l) : Vec3::Zero();
}

double TriMesh::faceArea(int f) const {
    const auto& t = F[f];
    return 0.5 * (V[t[1]] - V[t[0]]).cross(V[t[2]] - V[t[0]]).norm();
}

double TriMesh::totalArea() const {
    double a = 0;
    for (int f = 0; f < (int)F.size(); ++f) a += faceArea(f);
    return a;
}

std::vector<int> shortestVertexPath(const TriMesh& m, int from, int to,
                                    const std::vector<double>* edgeCost) {
    const double INF = std::numeric_limits<double>::infinity();
    std::vector<double> dist(m.V.size(), INF);
    std::vector<int> prev(m.V.size(), -1);
    using QE = std::pair<double, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<>> q;
    dist[from] = 0;
    q.push({0, from});
    while (!q.empty()) {
        auto [d, v] = q.top();
        q.pop();
        if (d > dist[v]) continue;
        if (v == to) break;
        for (int e : m.vertexEdges[v]) {
            int u = (m.edges[e].v0 == v) ? m.edges[e].v1 : m.edges[e].v0;
            double w = edgeCost ? (*edgeCost)[e] : m.edgeLength(e);
            if (dist[v] + w < dist[u]) {
                dist[u] = dist[v] + w;
                prev[u] = v;
                q.push({dist[u], u});
            }
        }
    }
    if (dist[to] == INF) return {};
    std::vector<int> path;
    for (int v = to; v != -1; v = prev[v]) path.push_back(v);
    std::reverse(path.begin(), path.end());
    return path;
}

} // namespace sf
