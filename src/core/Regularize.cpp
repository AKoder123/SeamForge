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
