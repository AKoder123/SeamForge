#include "Resimulate.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace sf {

namespace {

struct DistCon {
    int a, b;
    double rest;
};

// mean of one-way nearest-vertex distances, symmetric
double chamferDistance(const std::vector<Vec3>& A, const std::vector<Vec3>& B) {
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

// rasterise triangles projected along `axis` into a res x res occupancy grid
std::vector<char> rasterSilhouette(const std::vector<Vec3>& V,
                                   const std::vector<std::array<int, 3>>& F,
                                   int axis, int res,
                                   const Eigen::AlignedBox3d& box) {
    int u = (axis + 1) % 3, v = (axis + 2) % 3;
    double u0 = box.min()[u], v0 = box.min()[v];
    double du = std::max(box.max()[u] - u0, 1e-12) / res;
    double dv = std::max(box.max()[v] - v0, 1e-12) / res;
    std::vector<char> grid(res * res, 0);
    for (const auto& t : F) {
        double ax = (V[t[0]][u] - u0) / du, ay = (V[t[0]][v] - v0) / dv;
        double bx = (V[t[1]][u] - u0) / du, by = (V[t[1]][v] - v0) / dv;
        double cx = (V[t[2]][u] - u0) / du, cy = (V[t[2]][v] - v0) / dv;
        int xmin = std::max(0, (int)std::floor(std::min({ax, bx, cx})));
        int xmax = std::min(res - 1, (int)std::ceil(std::max({ax, bx, cx})));
        int ymin = std::max(0, (int)std::floor(std::min({ay, by, cy})));
        int ymax = std::min(res - 1, (int)std::ceil(std::max({ay, by, cy})));
        double det = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (std::abs(det) < 1e-12) continue;
        for (int y = ymin; y <= ymax; ++y)
            for (int x = xmin; x <= xmax; ++x) {
                double px = x + 0.5, py = y + 0.5;
                double w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / det;
                double w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / det;
                double w2 = 1.0 - w0 - w1;
                if (w0 >= -1e-9 && w1 >= -1e-9 && w2 >= -1e-9) grid[y * res + x] = 1;
            }
    }
    return grid;
}

double gridIoU(const std::vector<char>& A, const std::vector<char>& B) {
    int inter = 0, uni = 0;
    for (size_t i = 0; i < A.size(); ++i) {
        inter += A[i] && B[i];
        uni += A[i] || B[i];
    }
    return uni ? (double)inter / uni : 1.0;
}

} // namespace

ResimMetrics resimulateValidate(const Project& proj, const ResimOptions& opt) {
    ResimMetrics out;
    if (proj.panels.empty()) {
        out.message = "project has no panels";
        return out;
    }
    for (const auto& p : proj.panels)
        if (p.UV.size() != p.V.size()) {
            out.message = "panel " + std::to_string(p.id) + " is not flattened (no UV)";
            return out;
        }
    if (proj.relations.empty()) {
        out.message = "project has no seam relations";
        return out;
    }
    const double diag = proj.mesh.bbox().diagonal().norm();

    // ---- particle system ----------------------------------------------
    std::vector<int> panelOffset(proj.panels.size() + 1, 0);
    for (size_t pi = 0; pi < proj.panels.size(); ++pi)
        panelOffset[pi + 1] = panelOffset[pi] + (int)proj.panels[pi].V.size();
    const int n = panelOffset.back();

    std::vector<Vec3> x(n), source(n);
    std::vector<int> origV(n, -1);
    for (size_t pi = 0; pi < proj.panels.size(); ++pi) {
        const Panel& p = proj.panels[pi];
        for (size_t lv = 0; lv < p.V.size(); ++lv) {
            x[panelOffset[pi] + lv] = p.V[lv];
            source[panelOffset[pi] + lv] = p.V[lv];
            if (lv < p.toOrigV.size()) origV[panelOffset[pi] + lv] = p.toOrigV[lv];
        }
    }

    // panel id -> index in proj.panels
    auto panelIdx = [&](int id) -> int {
        for (size_t pi = 0; pi < proj.panels.size(); ++pi)
            if (proj.panels[pi].id == id) return (int)pi;
        return -1;
    };

    // distance constraints: pattern metric (UV edge lengths are the rest state)
    std::vector<DistCon> edges;
    for (size_t pi = 0; pi < proj.panels.size(); ++pi) {
        const Panel& p = proj.panels[pi];
        double uvScale = (pi == 0) ? opt.corruptPanel0Scale : 1.0;
        TriMesh pm = p.toTriMesh();
        for (const auto& e : pm.edges) {
            double rest = uvScale * (p.UV[e.v0] - p.UV[e.v1]).norm();
            edges.push_back({panelOffset[pi] + e.v0, panelOffset[pi] + e.v1, rest});
        }
    }

    // seam constraints: paired vertices coincide (proportional resampling
    // when the two sides carry different vertex counts)
    std::vector<DistCon> seams;
    for (const auto& r : proj.relations) {
        int pa = panelIdx(r.a.panelId), pb = panelIdx(r.b.panelId);
        if (pa < 0 || pb < 0) continue;
        const auto& A = r.a.localVerts;
        const auto& B = r.b.localVerts;
        if (A.size() < 2 || B.size() < 2) continue;
        for (size_t i = 0; i < A.size(); ++i) {
            size_t j = B.size() == A.size()
                           ? i
                           : (size_t)std::lround((double)i * (B.size() - 1) /
                                                 std::max<size_t>(1, A.size() - 1));
            seams.push_back({panelOffset[pa] + A[i], panelOffset[pb] + (int)B[j], 0.0});
        }
    }
    if (seams.empty()) {
        out.message = "no usable seam constraints";
        return out;
    }

    // ---- PBD relaxation (Gauss-Seidel, deterministic order) -------------
    auto project = [&](const DistCon& c) {
        Vec3 d = x[c.a] - x[c.b];
        double len = d.norm();
        if (len < 1e-15 && c.rest <= 0) return 0.0;
        Vec3 dir = len > 1e-15 ? Vec3(d / len) : Vec3(1, 0, 0);
        double C = len - c.rest;
        Vec3 corr = 0.5 * C * dir;
        x[c.a] -= corr;
        x[c.b] += corr;
        return std::abs(C);
    };
    int iter = 0;
    for (; iter < opt.iterations; ++iter) {
        double maxC = 0;
        for (const auto& c : seams) maxC = std::max(maxC, project(c));
        for (const auto& c : edges) maxC = std::max(maxC, project(c));
        if (maxC < opt.convergeEps * diag) {
            ++iter;
            break;
        }
    }
    out.iterationsRun = iter;

    // ---- rigid (Kabsch) alignment to the source -------------------------
    Vec3 cx = Vec3::Zero(), cs = Vec3::Zero();
    for (int i = 0; i < n; ++i) {
        cx += x[i];
        cs += source[i];
    }
    cx /= n;
    cs /= n;
    Eigen::Matrix3d H = Eigen::Matrix3d::Zero();
    for (int i = 0; i < n; ++i) H += (x[i] - cx) * (source[i] - cs).transpose();
    Eigen::JacobiSVD<Eigen::Matrix3d> svd(H, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::Matrix3d R = svd.matrixV() * svd.matrixU().transpose();
    if (R.determinant() < 0) {
        Eigen::Matrix3d V = svd.matrixV();
        V.col(2) *= -1;
        R = V * svd.matrixU().transpose();
    }
    for (int i = 0; i < n; ++i) x[i] = R * (x[i] - cx) + cs;

    // ---- metrics --------------------------------------------------------
    double se = 0;
    for (int i = 0; i < n; ++i) {
        double d = (x[i] - source[i]).norm();
        se += d * d;
        out.driftMax = std::max(out.driftMax, d);
    }
    out.driftRms = std::sqrt(se / n);
    out.driftRmsRel = diag > 0 ? out.driftRms / diag : 0;

    out.chamfer = chamferDistance(x, proj.mesh.V);

    double angSum = 0;
    int nf = 0;
    for (size_t pi = 0; pi < proj.panels.size(); ++pi) {
        const Panel& p = proj.panels[pi];
        for (size_t f = 0; f < p.F.size(); ++f) {
            const auto& t = p.F[f];
            Vec3 nRec = (x[panelOffset[pi] + t[1]] - x[panelOffset[pi] + t[0]])
                            .cross(x[panelOffset[pi] + t[2]] - x[panelOffset[pi] + t[0]]);
            int of = f < p.origFace.size() ? p.origFace[f] : -1;
            if (of < 0 || nRec.norm() < 1e-15) continue;
            Vec3 nSrc = proj.mesh.faceNormal(of);
            double c = std::clamp(nRec.normalized().dot(nSrc), -1.0, 1.0);
            angSum += std::acos(c) * 180.0 / std::numbers::pi;
            ++nf;
        }
    }
    out.meanNormalDeviationDeg = nf ? angSum / nf : 0;

    // silhouettes over the union bbox
    Eigen::AlignedBox3d box = proj.mesh.bbox();
    for (const auto& q : x) box.extend(q);
    std::vector<std::array<int, 3>> recF;
    for (size_t pi = 0; pi < proj.panels.size(); ++pi)
        for (const auto& t : proj.panels[pi].F)
            recF.push_back({panelOffset[pi] + t[0], panelOffset[pi] + t[1],
                            panelOffset[pi] + t[2]});
    double iou = 0;
    for (int axis = 0; axis < 3; ++axis) {
        auto gs = rasterSilhouette(proj.mesh.V, proj.mesh.F, axis, opt.silhouetteRes, box);
        auto gr = rasterSilhouette(x, recF, axis, opt.silhouetteRes, box);
        iou += gridIoU(gs, gr) / 3.0;
    }
    out.silhouetteIoU = iou;

    for (const auto& c : seams) {
        double g = (x[c.a] - x[c.b]).norm();
        out.meanSeamGap += g / seams.size();
        out.maxSeamGap = std::max(out.maxSeamGap, g);
    }

    out.success = true;
    return out;
}

} // namespace sf
