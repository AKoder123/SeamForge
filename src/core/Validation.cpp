#include "Validation.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <unordered_map>

namespace sf {

const char* issueTypeName(IssueType t) {
    switch (t) {
        case IssueType::DisconnectedComponents: return "disconnected-components";
        case IssueType::DuplicateVertices:      return "duplicate-vertices";
        case IssueType::UnreferencedVertices:   return "unreferenced-vertices";
        case IssueType::DegenerateTriangles:    return "degenerate-triangles";
        case IssueType::ThinTriangles:          return "thin-triangles";
        case IssueType::InconsistentWinding:    return "inconsistent-winding";
        case IssueType::NonOrientable:          return "non-orientable";
        case IssueType::NonManifoldEdges:       return "non-manifold-edges";
        case IssueType::BoundaryLoops:          return "boundary-loops";
        case IssueType::SmallHoles:             return "small-holes";
        case IssueType::SuspiciousScale:        return "suspicious-scale";
        case IssueType::Orientation:            return "orientation";
    }
    return "unknown";
}

bool ValidationReport::hasErrors() const {
    for (const auto& i : issues)
        if (i.severity == Severity::Error && !i.autoRepaired) return true;
    return false;
}

std::string ValidationReport::toText() const {
    std::ostringstream os;
    for (const auto& i : issues) {
        const char* sev = i.severity == Severity::Error ? "ERROR" :
                          i.severity == Severity::Warning ? "WARN " : "INFO ";
        os << sev << " [" << issueTypeName(i.type) << "] count=" << i.count
           << (i.autoRepaired ? " (auto-repaired) " : " ") << i.description << "\n";
    }
    if (issues.empty()) os << "no issues detected\n";
    return os.str();
}

namespace {

// Weld vertices whose positions coincide within eps using a spatial grid.
int weldVertices(TriMesh& m, double eps) {
    if (m.V.empty()) return 0;
    const double cell = std::max(eps, 1e-30);
    auto key = [&](const Vec3& p) {
        auto q = [&](double x) { return (std::int64_t)std::floor(x / cell); };
        return std::tuple<std::int64_t, std::int64_t, std::int64_t>(q(p.x()), q(p.y()), q(p.z()));
    };
    std::map<std::tuple<std::int64_t, std::int64_t, std::int64_t>, std::vector<int>> grid;
    std::vector<int> remap(m.V.size());
    std::vector<Vec3> newV;
    newV.reserve(m.V.size());
    int welded = 0;
    for (int i = 0; i < (int)m.V.size(); ++i) {
        const Vec3& p = m.V[i];
        int found = -1;
        auto [cx, cy, cz] = key(p);
        for (std::int64_t dx = -1; dx <= 1 && found < 0; ++dx)
            for (std::int64_t dy = -1; dy <= 1 && found < 0; ++dy)
                for (std::int64_t dz = -1; dz <= 1 && found < 0; ++dz) {
                    auto it = grid.find({cx + dx, cy + dy, cz + dz});
                    if (it == grid.end()) continue;
                    for (int j : it->second)
                        if ((newV[j] - p).norm() <= eps) { found = j; break; }
                }
        if (found >= 0) {
            remap[i] = found;
            ++welded;
        } else {
            remap[i] = (int)newV.size();
            grid[{cx, cy, cz}].push_back((int)newV.size());
            newV.push_back(p);
        }
    }
    if (welded == 0) return 0;
    m.V = std::move(newV);
    for (auto& f : m.F)
        for (int k = 0; k < 3; ++k) f[k] = remap[f[k]];
    return welded;
}

int dropUnreferenced(TriMesh& m) {
    std::vector<char> used(m.V.size(), 0);
    for (const auto& f : m.F)
        for (int k = 0; k < 3; ++k) used[f[k]] = 1;
    std::vector<int> remap(m.V.size(), -1);
    std::vector<Vec3> newV;
    for (int i = 0; i < (int)m.V.size(); ++i)
        if (used[i]) {
            remap[i] = (int)newV.size();
            newV.push_back(m.V[i]);
        }
    int dropped = (int)m.V.size() - (int)newV.size();
    if (dropped == 0) return 0;
    m.V = std::move(newV);
    for (auto& f : m.F)
        for (int k = 0; k < 3; ++k) f[k] = remap[f[k]];
    return dropped;
}

// Greedy BFS re-orientation. Returns {flipped, orientable}.
std::pair<int, bool> makeConsistentWinding(TriMesh& m) {
    m.buildAdjacency();
    // Directed-edge presence per face for orientation comparison.
    auto hasDirected = [&](int f, int a, int b) {
        const auto& t = m.F[f];
        for (int k = 0; k < 3; ++k)
            if (t[k] == a && t[(k + 1) % 3] == b) return true;
        return false;
    };
    std::vector<int> state(m.F.size(), 0);   // 0 unvisited, 1 keep, 2 flip
    int flipped = 0;
    bool orientable = true;
    std::vector<int> stack;
    for (int f0 = 0; f0 < (int)m.F.size(); ++f0) {
        if (state[f0]) continue;
        state[f0] = 1;
        stack.push_back(f0);
        while (!stack.empty()) {
            int f = stack.back();
            stack.pop_back();
            bool fFlipped = state[f] == 2;
            for (int k = 0; k < 3; ++k) {
                int e = m.faceEdges[f][k];
                if (m.edges[e].faces.size() != 2) continue;
                int g = m.edges[e].faces[0] == f ? m.edges[e].faces[1] : m.edges[e].faces[0];
                int a = m.F[f][k], b = m.F[f][(k + 1) % 3];
                // In face f (as stored) the directed edge is (a,b); if f is
                // logically flipped it is (b,a). Neighbour g must carry the
                // opposite direction to be consistent.
                bool fDir = !fFlipped;   // true: f effectively carries (a,b)
                bool gHasAB = hasDirected(g, a, b);
                // g is consistent iff it carries the opposite direction of f.
                int want = (fDir == gHasAB) ? 2 : 1;
                if (state[g] == 0) {
                    state[g] = want;
                    if (want == 2) ++flipped;
                    stack.push_back(g);
                } else if (state[g] != want) {
                    orientable = false;
                }
            }
        }
    }
    if (orientable && flipped > 0) {
        for (int f = 0; f < (int)m.F.size(); ++f)
            if (state[f] == 2) std::swap(m.F[f][1], m.F[f][2]);
        m.invalidateAdjacency();
    }
    return {flipped, orientable};
}

double triAspect(const Vec3& a, const Vec3& b, const Vec3& c) {
    double la = (b - c).norm(), lb = (a - c).norm(), lc = (a - b).norm();
    double lmax = std::max({la, lb, lc});
    double area = 0.5 * (b - a).cross(c - a).norm();
    if (area < 1e-30) return 1e30;
    // longest edge / corresponding height
    return lmax * lmax / (2.0 * area);
}

} // namespace

ValidationReport validateAndRepair(TriMesh& m, const ValidationOptions& opt) {
    ValidationReport rep;
    auto add = [&](IssueType t, Severity s, int count, bool repaired, std::string d) {
        if (count == 0) return;
        rep.issues.push_back({t, s, count, repaired, std::move(d)});
    };

    const double diag = m.V.empty() ? 0.0 : m.bbox().diagonal().norm();

    // 1. duplicate vertices
    if (opt.weldDuplicates && diag > 0) {
        int welded = weldVertices(m, opt.weldEpsilonRel * diag);
        add(IssueType::DuplicateVertices, Severity::Info, welded, true,
            "coincident vertices welded (tolerance " +
                std::to_string(opt.weldEpsilonRel * diag) + ")");
    }

    // 2. degenerate faces (repeated indices or ~zero area)
    {
        double meanArea = 0;
        for (int f = 0; f < (int)m.F.size(); ++f) {
            const auto& t = m.F[f];
            meanArea += 0.5 * (m.V[t[1]] - m.V[t[0]]).cross(m.V[t[2]] - m.V[t[0]]).norm();
        }
        meanArea /= std::max<size_t>(1, m.F.size());
        std::vector<std::array<int, 3>> keep;
        int degen = 0;
        for (const auto& t : m.F) {
            bool bad = t[0] == t[1] || t[1] == t[2] || t[0] == t[2];
            if (!bad) {
                double area = 0.5 * (m.V[t[1]] - m.V[t[0]]).cross(m.V[t[2]] - m.V[t[0]]).norm();
                bad = area < opt.degenerateAreaRel * meanArea;
            }
            if (bad) ++degen;
            if (!bad || !opt.dropDegenerateFaces) keep.push_back(t);
        }
        if (degen && opt.dropDegenerateFaces) {
            m.F = std::move(keep);
            add(IssueType::DegenerateTriangles, Severity::Warning, degen, true,
                "zero-area / repeated-index triangles removed");
        } else {
            add(IssueType::DegenerateTriangles, Severity::Error, degen, false,
                "degenerate triangles present (removal disabled)");
        }
    }

    // 3. unreferenced vertices (harmless; removed for compactness)
    add(IssueType::UnreferencedVertices, Severity::Info, dropUnreferenced(m), true,
        "vertices not used by any face removed");

    // 4. winding consistency / orientability
    {
        auto [flipped, orientable] = makeConsistentWinding(m);
        if (!orientable)
            add(IssueType::NonOrientable, Severity::Error, 1, false,
                "mesh is non-orientable; segmentation and flattening are undefined");
        else if (flipped > 0) {
            if (opt.fixWinding)
                add(IssueType::InconsistentWinding, Severity::Warning, flipped, true,
                    "faces re-oriented for consistent winding");
            else
                add(IssueType::InconsistentWinding, Severity::Error, flipped, false,
                    "inconsistent winding detected (repair disabled)");
        }
    }

    m.buildAdjacency();

    // 5. non-manifold edges — reported, never repaired (repair would be destructive)
    add(IssueType::NonManifoldEdges, Severity::Error, m.numNonManifoldEdges(), false,
        "edges shared by more than two faces; cut/flatten results are unreliable near them");

    // 6. connected components
    {
        int n = 0;
        m.faceComponents(n);
        if (n > 1)
            add(IssueType::DisconnectedComponents, Severity::Warning, n, false,
                "multiple connected components; each is treated independently");
    }

    // 7. boundary loops: expected garment openings vs suspicious small holes
    {
        auto loops = m.boundaryLoops();
        if (!loops.empty()) {
            auto loopLen = [&](const std::vector<int>& L) {
                double s = 0;
                for (size_t i = 0; i < L.size(); ++i)
                    s += (m.V[L[i]] - m.V[L[(i + 1) % L.size()]]).norm();
                return s;
            };
            double maxLen = 0;
            for (auto& L : loops) maxLen = std::max(maxLen, loopLen(L));
            int small = 0;
            for (auto& L : loops)
                if (loopLen(L) < opt.smallHoleFrac * maxLen) ++small;
            add(IssueType::BoundaryLoops, Severity::Info, (int)loops.size(),
                false, "open boundary loops (garment openings such as waist/hem are expected)");
            add(IssueType::SmallHoles, Severity::Warning, small, false,
                "very small boundary loops - possible mesh holes rather than garment openings");
        }
    }

    // 8. thin triangles (report only)
    {
        int thin = 0;
        for (const auto& t : m.F)
            if (triAspect(m.V[t[0]], m.V[t[1]], m.V[t[2]]) > opt.thinTriangleAspect) ++thin;
        add(IssueType::ThinTriangles, Severity::Warning, thin, false,
            "high-aspect triangles may destabilise flattening");
    }

    // 9. scale heuristic (metres expected; report only)
    if (diag > 0 && (diag < opt.expectedMinDiag || diag > opt.expectedMaxDiag))
        add(IssueType::SuspiciousScale, Severity::Warning, 1, false,
            "bounding-box diagonal " + std::to_string(diag) +
                " is outside the expected metre-scale range; check units");

    // 10. orientation heuristic (report only): garment 'up' assumed +Y
    if (!m.V.empty()) {
        Eigen::AlignedBox3d b = m.bbox();
        Vec3 ext = b.diagonal();
        int upAxis = 0;
        ext.cwiseAbs().maxCoeff(&upAxis);
        if (upAxis != 1)
            add(IssueType::Orientation, Severity::Info, 1, false,
                "longest bounding-box axis is not +Y; garment may not be upright");
    }

    return rep;
}

} // namespace sf
