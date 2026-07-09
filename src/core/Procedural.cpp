#include "Procedural.h"

#include <cmath>
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
    for (int r = 0; r <= M; ++r) {
        double t = (double)r / M;
        double radius = opt.waistRadius + t * (opt.hemRadius - opt.waistRadius);
        radius += opt.bulge * std::sin(t * std::numbers::pi);  // A-line curvature
        double y = (1.0 - t) * opt.height;
        for (int a = 0; a < N; ++a) {
            double ang = twoPi * a / N;
            Vec3 p(radius * std::cos(ang), y, radius * std::sin(ang));
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
    }
    return m;
}

} // namespace sf
