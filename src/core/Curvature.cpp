#include "Curvature.h"

#include <cmath>
#include <numbers>

namespace sf {

CurvatureField computeCurvature(const TriMesh& m) {
    CurvatureField cf;
    const int nv = (int)m.V.size();
    cf.gaussian.assign(nv, 0.0);
    cf.meanMagnitude.assign(nv, 0.0);
    cf.dihedral.assign(m.edges.size(), 0.0);

    std::vector<double> angleSum(nv, 0.0), area(nv, 0.0);
    std::vector<Vec3> lap(nv, Vec3::Zero());

    for (int f = 0; f < (int)m.F.size(); ++f) {
        const auto& t = m.F[f];
        double A = m.faceArea(f);
        for (int k = 0; k < 3; ++k) {
            int i = t[k], j = t[(k + 1) % 3], l = t[(k + 2) % 3];
            Vec3 e1 = (m.V[j] - m.V[i]).normalized();
            Vec3 e2 = (m.V[l] - m.V[i]).normalized();
            double ang = std::acos(std::clamp(e1.dot(e2), -1.0, 1.0));
            angleSum[i] += ang;
            area[i] += A / 3.0;
            // cotan contribution of the angle at l to edge (i,j)
            Vec3 u = m.V[i] - m.V[l], w = m.V[j] - m.V[l];
            double cross = u.cross(w).norm();
            double cot = cross > 1e-20 ? u.dot(w) / cross : 0.0;
            lap[i] += 0.5 * cot * (m.V[j] - m.V[i]);
            lap[j] += 0.5 * cot * (m.V[i] - m.V[j]);
        }
    }

    for (int i = 0; i < nv; ++i) {
        bool onBoundary = false;
        for (int e : m.vertexEdges[i])
            if (m.edges[e].boundary()) { onBoundary = true; break; }
        double defect = (onBoundary ? std::numbers::pi : 2.0 * std::numbers::pi) - angleSum[i];
        double a = std::max(area[i], 1e-20);
        cf.gaussian[i] = defect / a;
        cf.meanMagnitude[i] = lap[i].norm() / (2.0 * a);
    }

    for (size_t e = 0; e < m.edges.size(); ++e) {
        const Edge& ed = m.edges[e];
        if (ed.faces.size() != 2) continue;
        Vec3 n0 = m.faceNormal(ed.faces[0]);
        Vec3 n1 = m.faceNormal(ed.faces[1]);
        cf.dihedral[e] = std::acos(std::clamp(n0.dot(n1), -1.0, 1.0));
    }
    return cf;
}

} // namespace sf
