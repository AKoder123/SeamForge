#include "Flatten.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <algorithm>
#include <cmath>

namespace sf {

namespace {

// Local isometric 2D frame per triangle: p0 -> (0,0), p1 -> (l01, 0),
// p2 -> projection. Returns false for degenerate triangles.
bool triLocalFrame(const Vec3& p0, const Vec3& p1, const Vec3& p2,
                   Vec2& q0, Vec2& q1, Vec2& q2) {
    Vec3 e1 = p1 - p0;
    double l = e1.norm();
    if (l < 1e-20) return false;
    e1 /= l;
    Vec3 n = (p1 - p0).cross(p2 - p0);
    double nn = n.norm();
    if (nn < 1e-20) return false;
    n /= nn;
    Vec3 e2 = n.cross(e1);
    q0 = Vec2(0, 0);
    q1 = Vec2(l, 0);
    q2 = Vec2((p2 - p0).dot(e1), (p2 - p0).dot(e2));
    return true;
}

// Two boundary vertices with (approximately) maximal separation, for pinning.
std::pair<int, int> farthestBoundaryPair(const Panel& p) {
    TriMesh pm = p.toTriMesh();
    std::vector<int> bnd;
    for (const auto& loop : pm.boundaryLoops())
        for (int v : loop) bnd.push_back(v);
    if (bnd.size() < 2) {   // closed surface: fall back to any two vertices
        return {0, (int)p.V.size() - 1};
    }
    // heuristic: farthest from bnd[0], then farthest from that (diameter 2-sweep)
    auto farthest = [&](int from) {
        int best = bnd[0];
        double bd = -1;
        for (int v : bnd) {
            double d = (p.V[v] - p.V[from]).squaredNorm();
            if (d > bd) { bd = d; best = v; }
        }
        return best;
    };
    int a = farthest(bnd[0]);
    int b = farthest(a);
    return {a, b};
}

int countFlipped(const Panel& p, const std::vector<Vec2>& UV) {
    int flipped = 0;
    for (const auto& t : p.F) {
        Vec2 a = UV[t[1]] - UV[t[0]], b = UV[t[2]] - UV[t[0]];
        if (a.x() * b.y() - a.y() * b.x() <= 0) ++flipped;
    }
    return flipped;
}

// Deterministic frame normalisation: fix global reflection (majority of
// triangles positively oriented), rotate the principal axis of the UV
// point set to +Y, resolve the remaining sign ambiguities, translate the
// bbox min corner to the origin.
void normalizeFrame(const Panel& p, std::vector<Vec2>& UV) {
    double signedArea = 0;
    for (const auto& t : p.F) {
        Vec2 a = UV[t[1]] - UV[t[0]], b = UV[t[2]] - UV[t[0]];
        signedArea += 0.5 * (a.x() * b.y() - a.y() * b.x());
    }
    if (signedArea < 0)
        for (auto& q : UV) q.y() = -q.y();

    Vec2 mean = Vec2::Zero();
    for (const auto& q : UV) mean += q;
    mean /= (double)UV.size();
    Eigen::Matrix2d C = Eigen::Matrix2d::Zero();
    for (const auto& q : UV) {
        Vec2 d = q - mean;
        C += d * d.transpose();
    }
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> es(C);
    Vec2 axis = es.eigenvectors().col(1);   // largest eigenvalue
    // rotate axis -> +Y
    double c = axis.y(), s = axis.x();      // rotation by angle from axis to (0,1)
    for (auto& q : UV) {
        Vec2 d = q - mean;
        q = Vec2(c * d.x() - s * d.y(), s * d.x() + c * d.y());
    }
    // sign convention: heavier half on -Y, then on -X (deterministic)
    double sy = 0, sx = 0;
    for (const auto& q : UV) { sy += q.y() * std::abs(q.y()); sx += q.x() * std::abs(q.x()); }
    if (sy > 0) for (auto& q : UV) { q.y() = -q.y(); q.x() = -q.x(); }  // rotate 180 (keeps orientation)
    (void)sx;
    Vec2 mn(1e300, 1e300);
    for (const auto& q : UV) mn = mn.cwiseMin(q);
    for (auto& q : UV) q -= mn;
}

} // namespace

// ---------------------------------------------------------------- LSCM ----

FlattenResult LSCMFlattener::flatten(const Panel& p) const {
    FlattenResult r;
    const int nv = (int)p.V.size();
    const int nf = (int)p.F.size();
    if (nv < 3 || nf < 1) {
        r.message = "panel too small";
        return r;
    }

    auto [pinA, pinB] = farthestBoundaryPair(p);
    if (pinA == pinB) {
        r.message = "cannot select two distinct pin vertices";
        return r;
    }
    double pinDist = (p.V[pinA] - p.V[pinB]).norm();

    // unknowns: (u_i, v_i) for all vertices except the two pins
    // Build rows of the conformal energy: for each triangle with local frame
    // W_j = P_{j+2} - P_{j+1} (complex), the conformality residual is
    // sum_j W_j (u_j + i v_j) / sqrt(2 A_t).
    std::vector<int> col(nv, -1);
    int nu = 0;
    for (int i = 0; i < nv; ++i)
        if (i != pinA && i != pinB) col[i] = nu++;

    std::vector<Eigen::Triplet<double>> trips;
    trips.reserve(nf * 12);
    Eigen::VectorXd rhs = Eigen::VectorXd::Zero(2 * nf);
    // fixed values
    auto pinnedU = [&](int v) { return v == pinA ? 0.0 : pinDist; };
    auto pinnedV = [&](int) { return 0.0; };

    for (int f = 0; f < nf; ++f) {
        const auto& t = p.F[f];
        Vec2 q[3];
        if (!triLocalFrame(p.V[t[0]], p.V[t[1]], p.V[t[2]], q[0], q[1], q[2])) {
            r.message = "degenerate triangle in panel (face " + std::to_string(f) + ")";
            return r;
        }
        double area2 = (q[1] - q[0]).x() * (q[2] - q[0]).y() -
                       (q[1] - q[0]).y() * (q[2] - q[0]).x();   // = 2A
        double s = std::sqrt(std::max(area2, 1e-24));
        for (int j = 0; j < 3; ++j) {
            Vec2 W = (q[(j + 2) % 3] - q[(j + 1) % 3]) / s;
            int vtx = t[j];
            // rows: 2f (real), 2f+1 (imag)
            // real: Wx*u - Wy*v ; imag: Wy*u + Wx*v
            if (col[vtx] >= 0) {
                trips.emplace_back(2 * f,     2 * col[vtx],     W.x());
                trips.emplace_back(2 * f,     2 * col[vtx] + 1, -W.y());
                trips.emplace_back(2 * f + 1, 2 * col[vtx],     W.y());
                trips.emplace_back(2 * f + 1, 2 * col[vtx] + 1, W.x());
            } else {
                double uu = pinnedU(vtx), vv = pinnedV(vtx);
                rhs[2 * f]     -= W.x() * uu - W.y() * vv;
                rhs[2 * f + 1] -= W.y() * uu + W.x() * vv;
            }
        }
    }

    Eigen::SparseMatrix<double> A(2 * nf, 2 * nu);
    A.setFromTriplets(trips.begin(), trips.end());
    Eigen::SparseMatrix<double> AtA = Eigen::SparseMatrix<double>(A.transpose()) * A;
    Eigen::VectorXd Atb = A.transpose() * rhs;
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
    solver.compute(AtA);
    if (solver.info() != Eigen::Success) {
        r.message = "LSCM factorisation failed (panel topology unsuitable?)";
        return r;
    }
    Eigen::VectorXd x = solver.solve(Atb);
    if (solver.info() != Eigen::Success) {
        r.message = "LSCM solve failed";
        return r;
    }

    r.UV.assign(nv, Vec2::Zero());
    for (int i = 0; i < nv; ++i) {
        if (col[i] >= 0)
            r.UV[i] = Vec2(x[2 * col[i]], x[2 * col[i] + 1]);
        else
            r.UV[i] = Vec2(pinnedU(i), pinnedV(i));
    }
    r.finalEnergy = (A * x - rhs).squaredNorm();

    // LSCM fixes conformal structure but the global scale depends on the pin
    // choice; rescale so total 2D area matches total 3D area.
    double area2d = 0;
    for (const auto& t : p.F) {
        Vec2 a = r.UV[t[1]] - r.UV[t[0]], b = r.UV[t[2]] - r.UV[t[0]];
        area2d += 0.5 * (a.x() * b.y() - a.y() * b.x());
    }
    double area3d = p.area3d();
    if (std::abs(area2d) > 1e-20) {
        double sc = std::sqrt(area3d / std::abs(area2d));
        for (auto& q : r.UV) q *= sc;
    }

    normalizeFrame(p, r.UV);
    r.flippedTriangles = countFlipped(p, r.UV);
    r.success = true;
    if (r.flippedTriangles > 0)
        r.message = std::to_string(r.flippedTriangles) + " flipped triangles";
    return r;
}

// ---------------------------------------------------------------- ARAP ----

FlattenResult ARAPFlattener::flatten(const Panel& p) const {
    FlattenResult init = LSCMFlattener{}.flatten(p);
    if (!init.success) {
        init.message = "ARAP init failed: " + init.message;
        return init;
    }
    FlattenResult r;
    const int nv = (int)p.V.size();
    const int nf = (int)p.F.size();

    // Per-triangle local frames and cotan weights.
    std::vector<std::array<Vec2, 3>> X(nf);       // local 3D coords
    std::vector<std::array<double, 3>> W(nf);     // cotan weight of edge (j, j+1)
    for (int f = 0; f < nf; ++f) {
        const auto& t = p.F[f];
        if (!triLocalFrame(p.V[t[0]], p.V[t[1]], p.V[t[2]], X[f][0], X[f][1], X[f][2])) {
            r.message = "degenerate triangle in panel";
            return r;
        }
        for (int j = 0; j < 3; ++j) {
            // cotan of angle opposite edge (j, j+1), i.e. at vertex j+2
            Vec2 a = X[f][j]       - X[f][(j + 2) % 3];
            Vec2 b = X[f][(j + 1) % 3] - X[f][(j + 2) % 3];
            double cross = std::abs(a.x() * b.y() - a.y() * b.x());
            W[f][j] = cross > 1e-20 ? a.dot(b) / cross : 0.0;
            W[f][j] = std::clamp(W[f][j], -1e3, 1e3);
        }
    }

    // Global step matrix: cotan Laplacian with vertex 0 pinned.
    std::vector<Eigen::Triplet<double>> trips;
    Eigen::VectorXd diag = Eigen::VectorXd::Zero(nv);
    std::vector<Eigen::Triplet<double>> off;
    for (int f = 0; f < nf; ++f) {
        const auto& t = p.F[f];
        for (int j = 0; j < 3; ++j) {
            int a = t[j], b = t[(j + 1) % 3];
            double w = W[f][j];
            diag[a] += w;
            diag[b] += w;
            off.emplace_back(a, b, -w);
            off.emplace_back(b, a, -w);
        }
    }
    const int pin = 0;
    for (int i = 0; i < nv; ++i)
        trips.emplace_back(i, i, i == pin ? diag[i] + 1e9 : diag[i] + 1e-12);
    for (auto& t3 : off)
        trips.push_back(t3);
    Eigen::SparseMatrix<double> L(nv, nv);
    L.setFromTriplets(trips.begin(), trips.end());
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
    solver.compute(L);
    if (solver.info() != Eigen::Success) {
        r.message = "ARAP factorisation failed";
        return r;
    }

    std::vector<Vec2> UV = init.UV;
    Vec2 pinPos = UV[pin];
    double prevEnergy = 1e300;
    int iter = 0;
    for (; iter < maxIterations; ++iter) {
        // local step: per-triangle best rotation
        std::vector<Eigen::Matrix2d> R(nf);
        double energy = 0;
        for (int f = 0; f < nf; ++f) {
            const auto& t = p.F[f];
            Eigen::Matrix2d S = Eigen::Matrix2d::Zero();
            for (int j = 0; j < 3; ++j) {
                int a = t[j], b = t[(j + 1) % 3];
                Vec2 du = UV[a] - UV[b];
                Vec2 dx = X[f][j] - X[f][(j + 1) % 3];
                S += W[f][j] * du * dx.transpose();
            }
            Eigen::JacobiSVD<Eigen::Matrix2d> svd(S, Eigen::ComputeFullU | Eigen::ComputeFullV);
            Eigen::Matrix2d Rf = svd.matrixU() * svd.matrixV().transpose();
            if (Rf.determinant() < 0) {
                Eigen::Matrix2d U = svd.matrixU();
                U.col(1) *= -1;
                Rf = U * svd.matrixV().transpose();
            }
            R[f] = Rf;
            for (int j = 0; j < 3; ++j) {
                int a = t[j], b = t[(j + 1) % 3];
                Vec2 du = UV[a] - UV[b];
                Vec2 dx = X[f][j] - X[f][(j + 1) % 3];
                energy += W[f][j] * (du - Rf * dx).squaredNorm();
            }
        }
        // global step
        Eigen::VectorXd bx = Eigen::VectorXd::Zero(nv), by = Eigen::VectorXd::Zero(nv);
        for (int f = 0; f < nf; ++f) {
            const auto& t = p.F[f];
            for (int j = 0; j < 3; ++j) {
                int a = t[j], b = t[(j + 1) % 3];
                Vec2 e = R[f] * (X[f][j] - X[f][(j + 1) % 3]);
                bx[a] += W[f][j] * e.x(); by[a] += W[f][j] * e.y();
                bx[b] -= W[f][j] * e.x(); by[b] -= W[f][j] * e.y();
            }
        }
        bx[pin] += 1e9 * pinPos.x();
        by[pin] += 1e9 * pinPos.y();
        Eigen::VectorXd ux = solver.solve(bx), uy = solver.solve(by);
        if (solver.info() != Eigen::Success) {
            r.message = "ARAP global solve failed at iteration " + std::to_string(iter);
            return r;
        }
        for (int i = 0; i < nv; ++i) UV[i] = Vec2(ux[i], uy[i]);

        if (std::abs(prevEnergy - energy) < relTolerance * std::max(1.0, prevEnergy)) {
            prevEnergy = energy;
            ++iter;
            break;
        }
        prevEnergy = energy;
    }

    r.UV = std::move(UV);
    normalizeFrame(p, r.UV);
    r.iterations = iter;
    r.finalEnergy = prevEnergy;
    r.flippedTriangles = countFlipped(p, r.UV);
    r.success = true;
    if (r.flippedTriangles > 0)
        r.message = std::to_string(r.flippedTriangles) + " flipped triangles";
    return r;
}

std::unique_ptr<IFlattener> makeFlattener(const std::string& name) {
    if (name == "lscm") return std::make_unique<LSCMFlattener>();
    if (name == "arap") return std::make_unique<ARAPFlattener>();
    return nullptr;
}

// ----------------------------------------------------------- distortion ----

std::vector<FaceDistortion> computeDistortion(const Panel& p, const std::vector<Vec2>& UV) {
    std::vector<FaceDistortion> out(p.F.size());
    for (size_t f = 0; f < p.F.size(); ++f) {
        const auto& t = p.F[f];
        Vec2 q0, q1, q2;
        if (!triLocalFrame(p.V[t[0]], p.V[t[1]], p.V[t[2]], q0, q1, q2)) {
            out[f].flipped = true;
            continue;
        }
        Eigen::Matrix2d Dx, Du;
        Dx.col(0) = q1 - q0;  Dx.col(1) = q2 - q0;
        Du.col(0) = UV[t[1]] - UV[t[0]];
        Du.col(1) = UV[t[2]] - UV[t[0]];
        Eigen::Matrix2d J = Du * Dx.inverse();
        Eigen::JacobiSVD<Eigen::Matrix2d> svd(J);
        out[f].sigma1 = svd.singularValues()[0];
        out[f].sigma2 = svd.singularValues()[1];
        out[f].flipped = J.determinant() <= 0;
    }
    return out;
}

DistortionSummary summarizeDistortion(const Panel& p, const std::vector<Vec2>& UV,
                                      const std::vector<FaceDistortion>& fd) {
    DistortionSummary s;
    double totalArea = 0, sumAngle = 0, sumLogArea = 0;
    s.minAreaRatio = 1e300;
    s.maxAreaRatio = -1e300;
    s.maxStretch = 0;
    s.maxCompression = 1e300;
    for (size_t f = 0; f < fd.size(); ++f) {
        const auto& t = p.F[f];
        double A = 0.5 * (p.V[t[1]] - p.V[t[0]]).cross(p.V[t[2]] - p.V[t[0]]).norm();
        totalArea += A;
        if (fd[f].flipped) ++s.flipped;
        double ad = fd[f].angleDistortion();
        double ar = fd[f].areaRatio();
        sumAngle += A * ad;
        sumLogArea += A * std::abs(std::log(std::max(ar, 1e-20)));
        s.maxAngleDistortion = std::max(s.maxAngleDistortion, ad);
        s.minAreaRatio = std::min(s.minAreaRatio, ar);
        s.maxAreaRatio = std::max(s.maxAreaRatio, ar);
        s.maxStretch = std::max(s.maxStretch, fd[f].sigma1);
        s.maxCompression = std::min(s.maxCompression, fd[f].sigma2);
    }
    if (totalArea > 0) {
        s.meanAngleDistortion = sumAngle / totalArea;
        s.meanAbsLogArea = sumLogArea / totalArea;
    }
    // boundary length change
    TriMesh pm = p.toTriMesh();
    double worst = 0;
    for (const auto& loop : pm.boundaryLoops()) {
        double l3 = 0, l2 = 0;
        for (size_t i = 0; i < loop.size(); ++i) {
            int a = loop[i], b = loop[(i + 1) % loop.size()];
            l3 += (p.V[a] - p.V[b]).norm();
            l2 += (UV[a] - UV[b]).norm();
        }
        if (l3 > 0) worst = std::max(worst, std::abs(l2 - l3) / l3);
    }
    s.boundaryLengthChange = worst;
    return s;
}

} // namespace sf
