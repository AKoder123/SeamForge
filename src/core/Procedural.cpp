#include "Procedural.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <random>

namespace sf {

TriMesh makeSkirt(const SkirtOptions& opt, GroundTruth* gt) {
    TriMesh m;
    const int N = opt.radialSegments;
    const int M = opt.rings;
    const double twoPi = 2.0 * std::numbers::pi;

    std::mt19937 rng(opt.seed);
    std::uniform_real_distribution<double> jitter(-1.0, 1.0);

    // vertices: ring r in [0..M], angle index a in [0..N-1]
    auto vid = [&](int r, int a) { return r * N + ((a % N) + N) % N; };
    // dart creases at fixed angular columns on the front half (z > 0)
    std::vector<double> dartAngles;
    for (int d = 0; d < opt.darts; ++d) {
        double ang = std::numbers::pi / 2 + (d == 0 ? -1.0 : 1.0) * std::numbers::pi / 5;
        // snap to the nearest vertex column so the crease lies on mesh edges
        int col = (int)std::lround(ang / (twoPi / N));
        dartAngles.push_back(twoPi * col / N);
    }

    for (int r = 0; r <= M; ++r) {
        double t = (double)r / M;
        double radius = opt.waistRadius + t * (opt.hemRadius - opt.waistRadius);
        radius += opt.bulge * std::sin(t * std::numbers::pi);  // A-line curvature
        double y = (1.0 - t) * opt.height;
        for (int a = 0; a < N; ++a) {
            double ang = twoPi * a / N;
            double pinch = 0.0;
            for (double da : dartAngles) {
                double diff = std::abs(ang - da);
                diff = std::min(diff, twoPi - diff);
                double angular = std::max(0.0, 1.0 - diff / opt.dartWidth);
                double axial = std::max(0.0, 1.0 - t / opt.dartLength);
                pinch = std::max(pinch, opt.dartDepth * angular * axial);
            }
            double rr = radius * (1.0 - pinch);
            Vec3 p(rr * std::cos(ang), y, rr * std::sin(ang));
            if (opt.noise > 0) {
                double s = opt.noise * opt.height;
                p += s * Vec3(jitter(rng), jitter(rng), jitter(rng));
            }
            m.V.push_back(p);
        }
    }
    // faces (outward normals: CCW seen from outside)
    std::vector<int> faceSector;
    for (int r = 0; r < M; ++r) {
        for (int a = 0; a < N; ++a) {
            int v00 = vid(r, a),     v01 = vid(r, a + 1);
            int v10 = vid(r + 1, a), v11 = vid(r + 1, a + 1);
            m.F.push_back({v00, v01, v10});
            m.F.push_back({v01, v11, v10});
            double midAng = twoPi * (a + 0.5) / N;
            int sector = (int)(midAng / (twoPi / opt.panels)) % opt.panels;
            faceSector.push_back(sector);
            faceSector.push_back(sector);
        }
    }
    m.buildAdjacency();

    if (gt) {
        gt->faceLabel = faceSector;
        gt->panelCount = opt.panels;
        gt->seamPaths.clear();
        for (int k = 0; k < opt.panels; ++k) {
            int a = k * N / opt.panels;   // requires N divisible by panel count
            std::vector<int> path;
            for (int r = 0; r <= M; ++r) path.push_back(vid(r, a));
            gt->seamPaths.push_back(std::move(path));
        }
        gt->dartPaths.clear();
        for (double da : dartAngles) {
            int a = (int)std::lround(da / (twoPi / N)) % N;
            int lastRing = std::max(1, (int)std::floor(opt.dartLength * M));
            std::vector<int> path;
            for (int r = 0; r <= lastRing; ++r) path.push_back(vid(r, a));
            gt->dartPaths.push_back(std::move(path));
        }
    }
    return m;
}

// ---------------------------------------------------------------------------
// two-sheet sewn garments
// ---------------------------------------------------------------------------

namespace {

struct SheetSpec {
    int nx = 0, ny = 0;    // cells
    double cell = 0.02;
    double x0 = 0;         // world x of grid column i = 0 (y0 = 0)
    std::function<bool(double, double)> insideCell;   // cell-centre predicate
    std::function<bool(double, double)> isOpening;    // outline-vertex predicate
    double depth = 0.1;
    double margin = 0.06;
};

TriMesh buildTwoSheet(const SheetSpec& sp, GroundTruth* gt) {
    const int nvx = sp.nx + 1, nvy = sp.ny + 1;
    auto wx = [&](int i) { return sp.x0 + i * sp.cell; };
    auto wy = [&](int j) { return j * sp.cell; };
    auto cellInside = [&](int ci, int cj) {
        if (ci < 0 || cj < 0 || ci >= sp.nx || cj >= sp.ny) return false;
        return sp.insideCell(wx(ci) + 0.5 * sp.cell, wy(cj) + 0.5 * sp.cell);
    };
    auto gidx = [&](int i, int j) { return j * nvx + i; };

    // vertex usage and outline classification
    std::vector<char> used(nvx * nvy, 0), outline(nvx * nvy, 0);
    for (int cj = 0; cj < sp.ny; ++cj)
        for (int ci = 0; ci < sp.nx; ++ci) {
            if (!cellInside(ci, cj)) continue;
            for (int dj = 0; dj <= 1; ++dj)
                for (int di = 0; di <= 1; ++di) used[gidx(ci + di, cj + dj)] = 1;
        }
    // outline edges: grid edges bordering exactly one inside cell
    // horizontal edge (i,j)-(i+1,j): cells (i, j-1) and (i, j)
    // vertical   edge (i,j)-(i,j+1): cells (i-1, j) and (i, j)
    std::vector<std::vector<int>> outlineAdj(nvx * nvy);
    auto addOutlineEdge = [&](int a, int b) {
        outline[a] = outline[b] = 1;
        outlineAdj[a].push_back(b);
        outlineAdj[b].push_back(a);
    };
    for (int j = 0; j < nvy; ++j)
        for (int i = 0; i + 1 < nvx; ++i)
            if (cellInside(i, j - 1) != cellInside(i, j))
                addOutlineEdge(gidx(i, j), gidx(i + 1, j));
    for (int j = 0; j + 1 < nvy; ++j)
        for (int i = 0; i < nvx; ++i)
            if (cellInside(i - 1, j) != cellInside(i, j))
                addOutlineEdge(gidx(i, j), gidx(i, j + 1));

    std::vector<char> sewn(nvx * nvy, 0);
    std::vector<Vec3> sewnPos;
    for (int j = 0; j < nvy; ++j)
        for (int i = 0; i < nvx; ++i) {
            int g = gidx(i, j);
            if (!outline[g]) continue;
            sewn[g] = !sp.isOpening(wx(i), wy(j));
            if (sewn[g]) sewnPos.emplace_back(wx(i), wy(j), 0.0);
        }

    // bulge amplitude: tapers to zero at the sewn outline
    auto gap = [&](int i, int j) {
        double d = 1e300;
        Vec3 p(wx(i), wy(j), 0.0);
        for (const auto& q : sewnPos) d = std::min(d, (p - q).norm());
        return sp.depth * std::min(1.0, std::sqrt(d) / std::sqrt(sp.margin));
    };

    TriMesh m;
    std::vector<int> frontId(nvx * nvy, -1), backId(nvx * nvy, -1);
    for (int j = 0; j < nvy; ++j)
        for (int i = 0; i < nvx; ++i) {
            int g = gidx(i, j);
            if (!used[g]) continue;
            if (outline[g] && sewn[g]) {
                int id = (int)m.V.size();
                m.V.emplace_back(wx(i), wy(j), 0.0);
                frontId[g] = backId[g] = id;
            } else {
                double z = gap(i, j);
                frontId[g] = (int)m.V.size();
                m.V.emplace_back(wx(i), wy(j), z);
                backId[g] = (int)m.V.size();
                m.V.emplace_back(wx(i), wy(j), -z);
            }
        }

    std::vector<int> label;
    auto shared = [&](int g) { return frontId[g] == backId[g]; };
    for (int cj = 0; cj < sp.ny; ++cj)
        for (int ci = 0; ci < sp.nx; ++ci) {
            if (!cellInside(ci, cj)) continue;
            int g00 = gidx(ci, cj), g10 = gidx(ci + 1, cj);
            int g01 = gidx(ci, cj + 1), g11 = gidx(ci + 1, cj + 1);
            // pick the cell diagonal so it never connects two shared (sewn)
            // vertices: that would put four faces on one edge (non-manifold)
            bool diagA = !(shared(g00) && shared(g11));
            auto F = frontId;
            auto B = backId;
            if (diagA) {
                m.F.push_back({F[g00], F[g10], F[g11]});
                m.F.push_back({F[g00], F[g11], F[g01]});
                m.F.push_back({B[g00], B[g11], B[g10]});
                m.F.push_back({B[g00], B[g01], B[g11]});
            } else {
                m.F.push_back({F[g00], F[g10], F[g01]});
                m.F.push_back({F[g10], F[g11], F[g01]});
                m.F.push_back({B[g00], B[g01], B[g10]});
                m.F.push_back({B[g10], B[g01], B[g11]});
            }
            label.insert(label.end(), {0, 0, 1, 1});
        }
    m.buildAdjacency();

    if (gt) {
        gt->faceLabel = std::move(label);
        gt->panelCount = 2;
        gt->seamPaths.clear();
        gt->dartPaths.clear();
        // walk the outline loop and split into maximal sewn runs
        int start = -1;
        for (int g = 0; g < nvx * nvy && start < 0; ++g)
            if (outline[g]) start = g;
        if (start >= 0) {
            std::vector<int> loop;
            int prev = -1, cur = start;
            do {
                loop.push_back(cur);
                int next = -1;
                for (int nb : outlineAdj[cur])
                    if (nb != prev) { next = nb; break; }
                if (next < 0) break;   // open chain (should not happen)
                prev = cur;
                cur = next;
            } while (cur != start && loop.size() < outlineAdj.size() + 8);

            const int n = (int)loop.size();
            // rotate so index 0 is an opening vertex (run boundary)
            int rot = 0;
            for (int k = 0; k < n; ++k)
                if (!sewn[loop[k]]) { rot = k; break; }
            std::rotate(loop.begin(), loop.begin() + rot, loop.end());
            int k = 0;
            while (k < n) {
                if (!sewn[loop[k]]) { ++k; continue; }
                std::vector<int> run;
                while (k < n && sewn[loop[k]]) run.push_back(frontId[loop[k]]), ++k;
                if (run.size() >= 2) gt->seamPaths.push_back(std::move(run));
            }
        }
    }
    return m;
}

} // namespace

TriMesh makeBoxyTee(const BoxyTeeOptions& o, GroundTruth* gt) {
    SheetSpec sp;
    sp.cell = o.cell;
    sp.nx = (int)std::lround(2 * o.sleeveHalfSpan / o.cell);
    sp.ny = (int)std::lround(o.height / o.cell);
    sp.x0 = -o.sleeveHalfSpan;
    sp.depth = o.depth;
    sp.margin = o.taperMargin;
    sp.insideCell = [o](double x, double y) {
        bool torso = std::abs(x) <= o.torsoHalfWidth && y >= 0 && y <= o.height;
        bool sleeve = std::abs(x) <= o.sleeveHalfSpan && y >= o.armpitY && y <= o.height;
        return torso || sleeve;
    };
    const double eps = 0.51 * o.cell;
    sp.isOpening = [o, eps](double x, double y) {
        if (y > o.height - eps && std::abs(x) < o.neckHalfWidth) return true;  // neck
        if (y < eps) return true;                                              // waist hem
        if (std::abs(x) > o.sleeveHalfSpan - eps) return true;                 // cuffs
        return false;
    };
    return buildTwoSheet(sp, gt);
}

TriMesh makeFlatTrousers(const FlatTrousersOptions& o, GroundTruth* gt) {
    SheetSpec sp;
    sp.cell = o.cell;
    sp.nx = (int)std::lround(2 * o.hipHalfWidth / o.cell);
    sp.ny = (int)std::lround(o.height / o.cell);
    sp.x0 = -o.hipHalfWidth;
    sp.depth = o.depth;
    sp.margin = o.taperMargin;
    sp.insideCell = [o](double x, double y) {
        if (y < 0 || y > o.height || std::abs(x) > o.hipHalfWidth) return false;
        if (y >= o.crotchY) return true;                     // hip block
        return std::abs(x) >= o.legInnerGap;                 // legs
    };
    const double eps = 0.51 * o.cell;
    sp.isOpening = [o, eps](double x, double y) {
        (void)x;
        if (y > o.height - eps) return true;   // waist
        if (y < eps) return true;              // ankles
        return false;
    };
    return buildTwoSheet(sp, gt);
}

} // namespace sf
