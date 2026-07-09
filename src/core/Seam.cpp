#include "Seam.h"
#include "Curvature.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <unordered_set>

namespace sf {

double Seam::length3d(const TriMesh& m) const {
    double s = 0;
    for (size_t i = 0; i + 1 < vertices.size(); ++i)
        s += (m.V[vertices[i]] - m.V[vertices[i + 1]]).norm();
    return s;
}

std::string validateSeamPath(const TriMesh& m, const Seam& s) {
    if (s.vertices.size() < 2) return "seam has fewer than two vertices";
    for (size_t i = 0; i + 1 < s.vertices.size(); ++i) {
        int a = s.vertices[i], b = s.vertices[i + 1];
        if (a < 0 || b < 0 || a >= (int)m.V.size() || b >= (int)m.V.size())
            return "seam vertex index out of range";
        if (m.edgeBetween(a, b) < 0) {
            std::ostringstream os;
            os << "seam vertices " << a << " and " << b << " do not share a mesh edge";
            return os.str();
        }
    }
    std::unordered_set<int> seen;
    size_t last = s.vertices.size() - (s.closed() ? 1 : 0);
    for (size_t i = 0; i < last; ++i)
        if (!seen.insert(s.vertices[i]).second) return "seam path revisits a vertex";
    return {};
}

std::vector<int> seamEdgeIndices(const TriMesh& m, const Seam& s) {
    std::vector<int> es;
    for (size_t i = 0; i + 1 < s.vertices.size(); ++i) {
        int e = m.edgeBetween(s.vertices[i], s.vertices[i + 1]);
        if (e >= 0) es.push_back(e);
    }
    return es;
}

SeamProposalResult proposeSideSeams(const TriMesh& m) {
    SeamProposalResult out;
    std::ostringstream log;

    auto loops = m.boundaryLoops();
    if (loops.size() < 2) {
        log << "need at least two boundary loops (e.g. waist and hem); found "
            << loops.size() << "\n";
        out.log = log.str();
        return out;
    }
    const auto& loopA = loops[0];   // largest two loops = garment openings
    const auto& loopB = loops[1];

    auto centroid = [&](const std::vector<int>& L) {
        Vec3 c = Vec3::Zero();
        for (int v : L) c += m.V[v];
        return Vec3(c / (double)L.size());
    };
    Vec3 cA = centroid(loopA), cB = centroid(loopB);
    Vec3 up = (cA - cB).normalized();   // garment axis

    // widest horizontal direction = side-seam silhouette direction
    Vec3 seed = std::abs(up.x()) < 0.9 ? Vec3::UnitX() : Vec3::UnitZ();
    Vec3 side = (seed - up * up.dot(seed)).normalized();
    Vec3 axisOrigin = 0.5 * (cA + cB);
    double wMin = 1e300, wMax = -1e300;
    for (const auto& p : m.V) {
        double w = (p - axisOrigin).dot(side);
        wMin = std::min(wMin, w);
        wMax = std::max(wMax, w);
    }
    Vec3 alt = up.cross(side);
    double aMin = 1e300, aMax = -1e300;
    for (const auto& p : m.V) {
        double a = (p - axisOrigin).dot(alt);
        aMin = std::min(aMin, a);
        aMax = std::max(aMax, a);
    }
    if (aMax - aMin > wMax - wMin) {  // pick the wider horizontal axis
        side = alt;
        std::swap(wMin, aMin);
        std::swap(wMax, aMax);
    }
    log << "garment axis " << up.transpose() << ", side axis " << side.transpose() << "\n";

    // Edge costs: prefer paths that stay at the silhouette extreme and follow
    // curvature creases where present.
    CurvatureField cf = computeCurvature(m);
    double halfWidth = 0.5 * (wMax - wMin);

    auto extremeVertex = [&](const std::vector<int>& L, double sign) {
        int best = L[0];
        double bw = -1e300;
        for (int v : L) {
            double w = sign * (m.V[v] - axisOrigin).dot(side);
            if (w > bw) { bw = w; best = v; }
        }
        return best;
    };

    int sid = 0;
    for (double sign : {+1.0, -1.0}) {
        std::vector<double> cost(m.edges.size());
        for (size_t e = 0; e < m.edges.size(); ++e) {
            Vec3 mid = 0.5 * (m.V[m.edges[e].v0] + m.V[m.edges[e].v1]);
            double w = sign * (mid - axisOrigin).dot(side);
            double silhouettePenalty = (sign > 0 ? (wMax - w) : (w - wMin)) / std::max(halfWidth, 1e-12);
            double creaseBonus = cf.dihedral[e] / std::numbers::pi;   // [0,1)
            cost[e] = m.edgeLength(e) * (1.0 + 4.0 * silhouettePenalty) * (1.0 - 0.5 * creaseBonus);
            cost[e] = std::max(cost[e], 1e-12);
        }
        int vA = extremeVertex(loopA, sign);
        int vB = extremeVertex(loopB, sign);
        auto path = shortestVertexPath(m, vA, vB, &cost);
        if (path.size() < 2) {
            log << "no path between openings on side " << (sign > 0 ? "+" : "-") << "\n";
            continue;
        }
        // Curvature support: mean dihedral deviation along the path.
        double support = 0;
        int ne = 0;
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            int e = m.edgeBetween(path[i], path[i + 1]);
            if (e >= 0) { support += cf.dihedral[e]; ++ne; }
        }
        support = ne ? support / ne : 0.0;
        Seam s;
        s.id = sid++;
        s.vertices = std::move(path);
        s.source = Seam::Source::Proposed;
        // Base confidence from the silhouette prior alone is modest (0.35);
        // crease evidence along the path raises it, capped at 0.9. Never
        // report a proposal as certain.
        s.confidence = std::min(0.9, 0.35 + 2.0 * support / std::numbers::pi);
        std::ostringstream ev;
        ev << "silhouette-extreme geodesic (" << (sign > 0 ? "+" : "-")
           << "side), mean dihedral deviation " << support << " rad";
        s.evidence = ev.str();
        log << "proposed seam " << s.id << ": " << s.vertices.size()
            << " vertices, confidence " << s.confidence << "\n";
        out.seams.push_back(std::move(s));
    }
    std::sort(out.seams.begin(), out.seams.end(),
              [](const Seam& a, const Seam& b) { return a.confidence > b.confidence; });
    out.log = log.str();
    return out;
}

} // namespace sf
