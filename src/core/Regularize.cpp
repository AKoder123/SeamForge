#include "Regularize.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sf {

namespace {

double pointSegDist(const Vec2& p, const Vec2& a, const Vec2& b) {
    Vec2 ab = b - a;
    double l2 = ab.squaredNorm();
    if (l2 < 1e-24) return (p - a).norm();
    double t = std::clamp((p - a).dot(ab) / l2, 0.0, 1.0);
    return (p - (a + t * ab)).norm();
}

void dpRecurse(const std::vector<Vec2>& pts, int i0, int i1, double tol,
               std::vector<char>& keep) {
    if (i1 <= i0 + 1) return;
    double dmax = -1;
    int imax = -1;
    for (int i = i0 + 1; i < i1; ++i) {
        double d = pointSegDist(pts[i], pts[i0], pts[i1]);
        if (d > dmax) { dmax = d; imax = i; }
    }
    if (dmax > tol) {
        keep[imax] = 1;
        dpRecurse(pts, i0, imax, tol, keep);
        dpRecurse(pts, imax, i1, tol, keep);
    }
}

} // namespace

// ---- Schneider cubic fitting (Graphics Gems I, public-domain algorithm) ----

Vec2 bezierPoint(const BezierSegment& b, double t) {
    double s = 1 - t;
    return s * s * s * b.p0 + 3 * s * s * t * b.c0 + 3 * s * t * t * b.c1 +
           t * t * t * b.p1;
}

namespace {

double bezierLength(const BezierSegment& b) {
    const int N = 32;
    double len = 0;
    Vec2 prev = b.p0;
    for (int i = 1; i <= N; ++i) {
        Vec2 p = bezierPoint(b, (double)i / N);
        len += (p - prev).norm();
        prev = p;
    }
    return len;
}

// chord-length parameterisation of pts[first..last]
std::vector<double> chordParams(const std::vector<Vec2>& pts, int first, int last) {
    std::vector<double> u(last - first + 1, 0.0);
    for (int i = first + 1; i <= last; ++i)
        u[i - first] = u[i - first - 1] + (pts[i] - pts[i - 1]).norm();
    double total = u.back();
    if (total > 1e-30)
        for (auto& v : u) v /= total;
    return u;
}

BezierSegment generateBezier(const std::vector<Vec2>& pts, int first, int last,
                             const std::vector<double>& u, const Vec2& tHat1,
                             const Vec2& tHat2) {
    const int n = last - first + 1;
    BezierSegment bez;
    bez.p0 = pts[first];
    bez.p1 = pts[last];

    // least squares for alpha1, alpha2 (control-point distances along tangents)
    double C[2][2] = {{0, 0}, {0, 0}};
    double X[2] = {0, 0};
    for (int i = 0; i < n; ++i) {
        double t = u[i], s = 1 - t;
        Vec2 A1 = tHat1 * (3 * s * s * t);
        Vec2 A2 = tHat2 * (3 * s * t * t);
        C[0][0] += A1.dot(A1);
        C[0][1] += A1.dot(A2);
        C[1][1] += A2.dot(A2);
        Vec2 tmp = pts[first + i] -
                   (pts[first] * (s * s * s + 3 * s * s * t) +
                    pts[last] * (t * t * t + 3 * s * t * t));
        X[0] += A1.dot(tmp);
        X[1] += A2.dot(tmp);
    }
    C[1][0] = C[0][1];
    double det = C[0][0] * C[1][1] - C[0][1] * C[1][0];
    double alpha1 = 0, alpha2 = 0;
    if (std::abs(det) > 1e-18) {
        alpha1 = (X[0] * C[1][1] - X[1] * C[0][1]) / det;
        alpha2 = (C[0][0] * X[1] - C[1][0] * X[0]) / det;
    }
    double segLen = (pts[last] - pts[first]).norm();
    double eps = 1e-6 * segLen;
    if (alpha1 < eps || alpha2 < eps || !std::isfinite(alpha1) || !std::isfinite(alpha2)) {
        // Wu/Barsky heuristic fallback
        alpha1 = alpha2 = segLen / 3.0;
    }
    bez.c0 = bez.p0 + tHat1 * alpha1;
    bez.c1 = bez.p1 + tHat2 * alpha2;
    return bez;
}

double maxFitError(const std::vector<Vec2>& pts, int first, int last,
                   const BezierSegment& bez, const std::vector<double>& u,
                   int* splitPoint) {
    double maxDist = 0;
    *splitPoint = (first + last) / 2;
    for (int i = first + 1; i < last; ++i) {
        double d = (bezierPoint(bez, u[i - first]) - pts[i]).squaredNorm();
        if (d > maxDist) {
            maxDist = d;
            *splitPoint = i;
        }
    }
    return std::sqrt(maxDist);
}

// one Newton-Raphson step improving parameter values
void reparameterize(const std::vector<Vec2>& pts, int first, int last,
                    std::vector<double>& u, const BezierSegment& bez) {
    for (int i = 0; i <= last - first; ++i) {
        double t = u[i];
        Vec2 d = bezierPoint(bez, t) - pts[first + i];
        // derivatives
        double s = 1 - t;
        Vec2 d1 = 3 * s * s * (bez.c0 - bez.p0) + 6 * s * t * (bez.c1 - bez.c0) +
                  3 * t * t * (bez.p1 - bez.c1);
        Vec2 d2 = 6 * s * (bez.c1 - 2 * bez.c0 + bez.p0) +
                  6 * t * (bez.p1 - 2 * bez.c1 + bez.c0);
        double num = d.dot(d1);
        double den = d1.dot(d1) + d.dot(d2);
        if (std::abs(den) > 1e-18) u[i] = std::clamp(t - num / den, 0.0, 1.0);
    }
}

void fitCubicRecurse(const std::vector<Vec2>& pts, int first, int last,
                     Vec2 tHat1, Vec2 tHat2, double tol,
                     std::vector<BezierSegment>& out, double* maxDev, int depth) {
    if (last - first == 1 || depth > 24) {
        BezierSegment line;
        line.p0 = pts[first];
        line.p1 = pts[last];
        line.c0 = line.p0 + (line.p1 - line.p0) / 3.0;
        line.c1 = line.p0 + 2.0 * (line.p1 - line.p0) / 3.0;
        line.isLine = last - first == 1;
        out.push_back(line);
        return;
    }
    auto u = chordParams(pts, first, last);
    BezierSegment bez = generateBezier(pts, first, last, u, tHat1, tHat2);
    int split = 0;
    double err = maxFitError(pts, first, last, bez, u, &split);
    if (err < tol) {
        out.push_back(bez);
        *maxDev = std::max(*maxDev, err);
        return;
    }
    if (err < 4.0 * tol) {   // try reparameterisation before splitting
        for (int it = 0; it < 4; ++it) {
            reparameterize(pts, first, last, u, bez);
            bez = generateBezier(pts, first, last, u, tHat1, tHat2);
            err = maxFitError(pts, first, last, bez, u, &split);
            if (err < tol) {
                out.push_back(bez);
                *maxDev = std::max(*maxDev, err);
                return;
            }
        }
    }
    split = std::clamp(split, first + 1, last - 1);
    Vec2 centerTangent = (pts[split - 1] - pts[split + 1]).normalized();
    fitCubicRecurse(pts, first, split, tHat1, centerTangent, tol, out, maxDev, depth + 1);
    fitCubicRecurse(pts, split, last, -centerTangent, tHat2, tol, out, maxDev, depth + 1);
}

double polylineLength(const std::vector<Vec2>& pts, int first, int last) {
    double s = 0;
    for (int i = first; i < last; ++i) s += (pts[i + 1] - pts[i]).norm();
    return s;
}

} // namespace

double curveChainLength(const std::vector<BezierSegment>& curves) {
    double s = 0;
    for (const auto& b : curves) s += b.isLine ? (b.p1 - b.p0).norm() : bezierLength(b);
    return s;
}

void fitLoopCurves(RegularizedLoop& reg, const CurveFitOptions& opt) {
    reg.curves.clear();
    reg.curveMaxDeviation = 0;
    reg.curveMaxLengthError = 0;
    const auto& raw = reg.raw;
    const int n = (int)raw.size();
    if (n < 4 || reg.keptIdx.empty()) return;

    // span boundaries: corner kept-points; a smooth closed loop without
    // corners is split at two far-apart kept points (C0 there, documented)
    std::vector<int> breaks;   // indices into raw
    for (size_t k = 0; k < reg.keptIdx.size(); ++k)
        if (reg.isCorner[k]) breaks.push_back(reg.keptIdx[k]);
    if (breaks.size() < 2) {
        breaks.clear();
        breaks.push_back(reg.keptIdx[0]);
        breaks.push_back(reg.keptIdx[reg.keptIdx.size() / 2]);
        std::sort(breaks.begin(), breaks.end());
        if (breaks[0] == breaks[1]) return;
    }

    for (size_t bi = 0; bi < breaks.size(); ++bi) {
        int i0 = breaks[bi];
        int i1 = breaks[(bi + 1) % breaks.size()];
        // unroll the (possibly wrapping) span into a contiguous point run
        std::vector<Vec2> pts;
        int i = i0;
        pts.push_back(raw[i]);
        while (i != i1) {
            i = (i + 1) % n;
            pts.push_back(raw[i]);
        }
        if (pts.size() < 2) continue;
        int last = (int)pts.size() - 1;

        // straight span -> true line segment
        double lineDev = 0;
        for (int j = 1; j < last; ++j)
            lineDev = std::max(lineDev, pointSegDist(pts[j], pts[0], pts[last]));
        double spanRawLen = polylineLength(pts, 0, last);
        if (lineDev < opt.lineDevFrac * opt.tolerance) {
            BezierSegment line;
            line.p0 = pts[0];
            line.p1 = pts[last];
            line.c0 = line.p0 + (line.p1 - line.p0) / 3.0;
            line.c1 = line.p0 + 2.0 * (line.p1 - line.p0) / 3.0;
            line.isLine = true;
            reg.curves.push_back(line);
            reg.curveMaxDeviation = std::max(reg.curveMaxDeviation, lineDev);
            double lenErr = spanRawLen > 1e-30
                                ? std::abs((line.p1 - line.p0).norm() - spanRawLen) / spanRawLen
                                : 0.0;
            reg.curveMaxLengthError = std::max(reg.curveMaxLengthError, lenErr);
            continue;
        }

        Vec2 tHat1 = (pts[1] - pts[0]).normalized();
        Vec2 tHat2 = (pts[last - 1] - pts[last]).normalized();

        // fit; tighten tolerance until the span's arc length is compatible
        // with the raw boundary (seam-length budget)
        double tol = opt.tolerance;
        std::vector<BezierSegment> spanCurves;
        double spanDev = 0, lenErr = 0;
        for (int attempt = 0; attempt < 4; ++attempt) {
            spanCurves.clear();
            spanDev = 0;
            fitCubicRecurse(pts, 0, last, tHat1, tHat2, tol, spanCurves, &spanDev, 0);
            double fitLen = curveChainLength(spanCurves);
            lenErr = spanRawLen > 1e-30 ? std::abs(fitLen - spanRawLen) / spanRawLen : 0.0;
            if (lenErr <= opt.maxLengthError) break;
            tol *= 0.25;
        }
        reg.curves.insert(reg.curves.end(), spanCurves.begin(), spanCurves.end());
        reg.curveMaxDeviation = std::max(reg.curveMaxDeviation, spanDev);
        reg.curveMaxLengthError = std::max(reg.curveMaxLengthError, lenErr);
    }
}

RegularizedLoop regularizeLoop(const std::vector<Vec2>& loop,
                               const RegularizeOptions& opt) {
    RegularizedLoop out;
    out.raw = loop;
    const int n = (int)loop.size();
    if (n < 4) {
        for (int i = 0; i < n; ++i) out.keptIdx.push_back(i);
        out.simplified = loop;
        out.isCorner.assign(n, true);
        out.isStraight.assign(n, false);
        return out;
    }

    // split the closed loop at the two mutually farthest points, then run
    // Douglas-Peucker on each half
    int a = 0, b = n / 2;
    {
        double best = -1;
        for (int i = 0; i < n; ++i) {
            // 2-sweep approximation of the diameter
            double d = (loop[i] - loop[0]).squaredNorm();
            if (d > best) { best = d; a = i; }
        }
        best = -1;
        for (int i = 0; i < n; ++i) {
            double d = (loop[i] - loop[a]).squaredNorm();
            if (d > best) { best = d; b = i; }
        }
        if (a > b) std::swap(a, b);
    }
    std::vector<char> keep(n, 0);
    keep[a] = keep[b] = 1;
    dpRecurse(loop, a, b, opt.tolerance, keep);
    // wrap-around half: b .. n-1, 0 .. a  -> use a rotated copy
    {
        std::vector<Vec2> rot;
        std::vector<int> idx;
        for (int i = b; i < n; ++i) { rot.push_back(loop[i]); idx.push_back(i); }
        for (int i = 0; i <= a; ++i) { rot.push_back(loop[i]); idx.push_back(i); }
        std::vector<char> keep2(rot.size(), 0);
        keep2.front() = keep2.back() = 1;
        dpRecurse(rot, 0, (int)rot.size() - 1, opt.tolerance, keep2);
        for (size_t i = 0; i < rot.size(); ++i)
            if (keep2[i]) keep[idx[i]] = 1;
    }

    for (int i = 0; i < n; ++i)
        if (keep[i]) out.keptIdx.push_back(i);
    for (int i : out.keptIdx) out.simplified.push_back(loop[i]);

    const int m = (int)out.keptIdx.size();
    // corners: turning angle at each kept point
    out.isCorner.assign(m, false);
    for (int i = 0; i < m; ++i) {
        Vec2 prev = out.simplified[(i + m - 1) % m];
        Vec2 cur = out.simplified[i];
        Vec2 next = out.simplified[(i + 1) % m];
        Vec2 d0 = (cur - prev).normalized();
        Vec2 d1 = (next - cur).normalized();
        double turn = std::acos(std::clamp(d0.dot(d1), -1.0, 1.0)) * 180.0 / std::numbers::pi;
        out.isCorner[i] = turn > opt.cornerAngleDeg;
    }
    // straightness + max deviation per kept segment
    out.isStraight.assign(m, false);
    for (int i = 0; i < m; ++i) {
        int r0 = out.keptIdx[i];
        int r1 = out.keptIdx[(i + 1) % m];
        double dmax = 0;
        int j = r0;
        while (j != r1) {
            dmax = std::max(dmax, pointSegDist(loop[j], loop[r0], loop[r1]));
            j = (j + 1) % n;
        }
        out.isStraight[i] = dmax < opt.straightDevFrac * opt.tolerance;
        out.maxDeviation = std::max(out.maxDeviation, dmax);
    }
    return out;
}

} // namespace sf
