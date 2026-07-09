#include "Matching.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

namespace sf {

namespace {

struct Arc {
    int panelIdx = -1;             // index into `panels`
    std::vector<int> localVerts;   // ordered along the loop; open arcs include both corner endpoints
    bool closed = false;           // whole loop without corners
    double len = 0;
};

double polyLen(const Panel& p, const std::vector<int>& verts, bool closed) {
    double s = 0;
    for (size_t i = 0; i + 1 < verts.size(); ++i)
        s += (p.V[verts[i]] - p.V[verts[i + 1]]).norm();
    if (closed && verts.size() > 2)
        s += (p.V[verts.back()] - p.V[verts.front()]).norm();
    return s;
}

// Split one boundary loop into arcs at 3D corners.
std::vector<Arc> splitLoop(const Panel& p, int panelIdx, const std::vector<int>& loop,
                           double cornerAngleDeg) {
    const int n = (int)loop.size();
    std::vector<int> corners;
    for (int i = 0; i < n; ++i) {
        const Vec3& prev = p.V[loop[(i + n - 1) % n]];
        const Vec3& cur = p.V[loop[i]];
        const Vec3& next = p.V[loop[(i + 1) % n]];
        Vec3 d0 = (cur - prev), d1 = (next - cur);
        double l0 = d0.norm(), l1 = d1.norm();
        if (l0 < 1e-20 || l1 < 1e-20) continue;
        double turn = std::acos(std::clamp(d0.dot(d1) / (l0 * l1), -1.0, 1.0)) *
                      180.0 / std::numbers::pi;
        if (turn > cornerAngleDeg) corners.push_back(i);
    }
    std::vector<Arc> arcs;
    if (corners.size() < 2) {
        Arc a;
        a.panelIdx = panelIdx;
        a.localVerts = loop;
        a.closed = true;
        a.len = polyLen(p, loop, true);
        arcs.push_back(std::move(a));
        return arcs;
    }
    for (size_t c = 0; c < corners.size(); ++c) {
        int i0 = corners[c], i1 = corners[(c + 1) % corners.size()];
        Arc a;
        a.panelIdx = panelIdx;
        int i = i0;
        a.localVerts.push_back(loop[i]);
        while (i != i1) {
            i = (i + 1) % n;
            a.localVerts.push_back(loop[i]);
        }
        if (a.localVerts.size() < 2) continue;
        a.len = polyLen(p, a.localVerts, false);
        arcs.push_back(std::move(a));
    }
    return arcs;
}

// K points sampled uniformly by arc length along the arc's 3D polyline.
std::vector<Vec3> samplePoints(const Panel& p, const Arc& arc, int K) {
    std::vector<Vec3> pts;
    std::vector<int> verts = arc.localVerts;
    if (arc.closed) verts.push_back(verts.front());
    std::vector<double> cum(verts.size(), 0.0);
    for (size_t i = 1; i < verts.size(); ++i)
        cum[i] = cum[i - 1] + (p.V[verts[i]] - p.V[verts[i - 1]]).norm();
    double total = cum.back();
    if (total < 1e-20) return {p.V[verts[0]]};
    size_t seg = 0;
    for (int k = 0; k < K; ++k) {
        double target = total * k / (K - 1.0);
        while (seg + 1 < cum.size() - 1 && cum[seg + 1] < target) ++seg;
        double t = (target - cum[seg]) / std::max(cum[seg + 1] - cum[seg], 1e-20);
        t = std::clamp(t, 0.0, 1.0);
        pts.push_back(p.V[verts[seg]] + t * (p.V[verts[seg + 1]] - p.V[verts[seg]]));
    }
    return pts;
}

// symmetric mean nearest-neighbour distance between two sample sets
double chamfer(const std::vector<Vec3>& A, const std::vector<Vec3>& B) {
    auto oneWay = [](const std::vector<Vec3>& X, const std::vector<Vec3>& Y) {
        double s = 0;
        for (const auto& x : X) {
            double best = 1e300;
            for (const auto& y : Y) best = std::min(best, (x - y).squaredNorm());
            s += std::sqrt(best);
        }
        return s / (double)X.size();
    };
    return 0.5 * (oneWay(A, B) + oneWay(B, A));
}

} // namespace

BoundaryMatchResult proposeBoundaryMatches(const std::vector<Panel>& panels,
                                           const BoundaryMatchOptions& opt) {
    BoundaryMatchResult out;
    std::ostringstream log;
    if (panels.size() < 2) {
        log << "need at least two panels to match boundaries; got " << panels.size() << "\n";
        out.log = log.str();
        return out;
    }

    // collect arcs from boundary runs that are NOT cut ancestry (seamId == -1)
    std::vector<Arc> arcs;
    for (size_t pi = 0; pi < panels.size(); ++pi) {
        const Panel& p = panels[pi];
        auto loops = p.boundaryLoopsLocal;
        if (loops.empty()) loops = p.toTriMesh().boundaryLoops();
        // panels with cut-ancestry segments: only free (seamId == -1) runs
        // are candidates; a fully free loop is split geometrically instead
        bool hasSeamAncestry = false;
        for (const auto& seg : p.segments) hasSeamAncestry |= seg.seamId >= 0;
        if (hasSeamAncestry) {
            for (const auto& seg : p.segments) {
                if (seg.seamId >= 0 || seg.localVerts.size() < 2) continue;
                Arc a;
                a.panelIdx = (int)pi;
                a.localVerts = seg.localVerts;
                a.len = polyLen(p, a.localVerts, false);
                arcs.push_back(std::move(a));
            }
        } else {
            for (const auto& loop : loops) {
                auto la = splitLoop(p, (int)pi, loop, opt.cornerAngleDeg);
                arcs.insert(arcs.end(), la.begin(), la.end());
            }
        }
    }
    log << arcs.size() << " candidate boundary arcs across " << panels.size() << " panels\n";

    // score all cross-panel pairs
    struct Cand {
        int a, b;
        double score, relDist;
        bool reversed;
    };
    std::vector<Cand> cands;
    std::vector<std::vector<Vec3>> samples(arcs.size());
    for (size_t i = 0; i < arcs.size(); ++i)
        samples[i] = samplePoints(panels[arcs[i].panelIdx], arcs[i], opt.samples);

    for (size_t i = 0; i < arcs.size(); ++i) {
        for (size_t j = i + 1; j < arcs.size(); ++j) {
            if (arcs[i].panelIdx == arcs[j].panelIdx) continue;
            double lenRatio = std::min(arcs[i].len, arcs[j].len) /
                              std::max(std::max(arcs[i].len, arcs[j].len), 1e-20);
            if (lenRatio < opt.minLengthRatio) continue;
            double d = chamfer(samples[i], samples[j]);
            double relDist = d / std::max(std::max(arcs[i].len, arcs[j].len), 1e-20);
            if (relDist > opt.maxRelDistance) continue;
            // orientation by endpoint correspondence (open arcs only)
            bool reversed = false;
            if (!arcs[i].closed && !arcs[j].closed) {
                const Panel& pa = panels[arcs[i].panelIdx];
                const Panel& pb = panels[arcs[j].panelIdx];
                Vec3 aS = pa.V[arcs[i].localVerts.front()], aE = pa.V[arcs[i].localVerts.back()];
                Vec3 bS = pb.V[arcs[j].localVerts.front()], bE = pb.V[arcs[j].localVerts.back()];
                double same = (aS - bS).norm() + (aE - bE).norm();
                double cross = (aS - bE).norm() + (aE - bS).norm();
                reversed = cross < same;
            }
            double score = lenRatio * (1.0 - relDist / opt.maxRelDistance);
            cands.push_back({(int)i, (int)j, score, relDist, reversed});
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
        if (x.score != y.score) return x.score > y.score;
        return std::tie(x.a, x.b) < std::tie(y.a, y.b);   // deterministic tie-break
    });

    // greedy assignment, each arc used at most once
    std::vector<char> used(arcs.size(), 0);
    int seamId = opt.firstSeamId;
    for (size_t ci = 0; ci < cands.size(); ++ci) {
        const Cand& c = cands[ci];
        if (used[c.a] || used[c.b]) continue;
        used[c.a] = used[c.b] = 1;

        // ambiguity: another still-plausible candidate sharing an arc
        std::string ambiguity;
        for (size_t cj = ci + 1; cj < cands.size(); ++cj) {
            const Cand& o = cands[cj];
            if (o.score < opt.ambiguityFactor * c.score) break;
            if (o.a == c.a || o.b == c.a || o.a == c.b || o.b == c.b) {
                ambiguity = "ambiguous: competing candidate with score " +
                            std::to_string(o.score) + " (winner " +
                            std::to_string(c.score) + ")";
                break;
            }
        }

        const Arc& A = arcs[c.a];
        const Arc& B = arcs[c.b];
        const Panel& pa = panels[A.panelIdx];
        const Panel& pb = panels[B.panelIdx];

        SeamRelation rel;
        rel.seamId = seamId;
        rel.a.panelId = pa.id;
        rel.a.localVerts = A.localVerts;
        rel.b.panelId = pb.id;
        rel.b.localVerts = B.localVerts;
        if (c.reversed)   // co-orient side B with side A
            std::reverse(rel.b.localVerts.begin(), rel.b.localVerts.end());
        rel.reversed = false;
        rel.source = "boundary-matching";
        rel.confidence = std::min(opt.maxConfidence,
                                  ambiguity.empty() ? c.score : 0.6 * c.score);
        rel.length3d = 0.5 * (A.len + B.len);
        rel.note = ambiguity;
        if (A.closed || B.closed) {
            rel.note += (rel.note.empty() ? "" : "; ");
            rel.note += "closed-loop match: orientation and start point unresolved";
            rel.confidence = std::min(rel.confidence, 0.5);
        }
        if (std::abs(A.len - B.len) / std::max(A.len, B.len) > 0.02) {
            rel.note += (rel.note.empty() ? "" : "; ");
            rel.note += "arc lengths differ by " +
                        std::to_string(100.0 * std::abs(A.len - B.len) /
                                       std::max(A.len, B.len)) +
                        "% - partial seam or easing?";
        }

        Seam s;
        s.id = seamId;
        s.source = Seam::Source::MeshBoundary;
        s.confidence = rel.confidence;
        s.evidence = "boundary match: length ratio " +
                     std::to_string(std::min(A.len, B.len) / std::max(A.len, B.len)) +
                     ", mean gap " + std::to_string(c.relDist * 100) + "% of length";
        for (int lv : A.localVerts) s.vertices.push_back(pa.toOrigV[lv]);

        log << "match seam " << seamId << ": panel " << pa.id << " <-> panel "
            << pb.id << ", score " << c.score << ", conf " << rel.confidence
            << (c.reversed ? ", direction reversed" : "")
            << (rel.note.empty() ? "" : (", " + rel.note)) << "\n";

        out.relations.push_back(std::move(rel));
        out.seams.push_back(std::move(s));
        ++seamId;
    }

    for (size_t i = 0; i < arcs.size(); ++i) {
        if (used[i]) continue;
        std::ostringstream u;
        u << "panel " << panels[arcs[i].panelIdx].id << " arc ("
          << arcs[i].localVerts.size() << " verts, length " << arcs[i].len
          << (arcs[i].closed ? ", closed loop" : "") << ") unmatched";
        out.unmatchedArcs.push_back(u.str());
        log << out.unmatchedArcs.back() << "\n";
    }
    out.log = log.str();
    return out;
}

} // namespace sf
