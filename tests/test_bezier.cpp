#include "core/Flatten.h"
#include "core/Procedural.h"
#include "core/Regularize.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

using namespace sf;

namespace {

std::vector<Vec2> rectangle(double w, double h, int perEdge) {
    std::vector<Vec2> pts;
    auto edge = [&](Vec2 a, Vec2 b) {
        for (int i = 0; i < perEdge; ++i)
            pts.push_back(a + ((double)i / perEdge) * (b - a));
    };
    edge({0, 0}, {w, 0});
    edge({w, 0}, {w, h});
    edge({w, h}, {0, h});
    edge({0, h}, {0, 0});
    return pts;
}

// flattened skirt panels: real pipeline boundaries (arcs + straight seams)
std::vector<RegularizedLoop> skirtPanelLoops() {
    GroundTruth gt;
    TriMesh m = makeSkirt({}, &gt);
    std::vector<Seam> seams;
    for (size_t i = 0; i < gt.seamPaths.size(); ++i) {
        Seam s;
        s.id = (int)i;
        s.vertices = gt.seamPaths[i];
        seams.push_back(std::move(s));
    }
    auto seg = segmentBySeams(m, seams);
    ARAPFlattener arap;
    RegularizeOptions ro;
    ro.tolerance = 0.004 * m.bbox().diagonal().norm();
    CurveFitOptions cfo;
    cfo.tolerance = ro.tolerance;
    std::vector<RegularizedLoop> loops;
    for (auto& p : seg.panels) {
        p.UV = arap.flatten(p).UV;
        TriMesh pm = p.toTriMesh();
        for (const auto& loop : pm.boundaryLoops()) {
            std::vector<Vec2> pts;
            for (int v : loop) pts.push_back(p.UV[v]);
            auto reg = regularizeLoop(pts, ro);
            fitLoopCurves(reg, cfo);
            loops.push_back(std::move(reg));
        }
    }
    return loops;
}

double rawLength(const std::vector<Vec2>& raw) {
    double s = 0;
    for (size_t i = 0; i < raw.size(); ++i)
        s += (raw[(i + 1) % raw.size()] - raw[i]).norm();
    return s;
}

} // namespace

TEST_CASE("rectangle fits as four true lines", "[bezier]") {
    auto loop = rectangle(0.4, 0.6, 40);
    RegularizeOptions ro;
    ro.tolerance = 0.003;
    auto reg = regularizeLoop(loop, ro);
    CurveFitOptions cfo;
    cfo.tolerance = ro.tolerance;
    fitLoopCurves(reg, cfo);
    REQUIRE(reg.curves.size() == 4);
    for (const auto& b : reg.curves) REQUIRE(b.isLine);
    // chain is closed and connected
    for (size_t i = 0; i < reg.curves.size(); ++i) {
        const auto& cur = reg.curves[i];
        const auto& next = reg.curves[(i + 1) % reg.curves.size()];
        REQUIRE((cur.p1 - next.p0).norm() < 1e-12);
    }
    REQUIRE(reg.curveMaxLengthError < 0.005);
}

TEST_CASE("skirt panel boundary: curved arcs become few smooth cubics, seams stay lines",
          "[bezier]") {
    auto loops = skirtPanelLoops();
    REQUIRE(loops.size() == 2);
    for (const auto& reg : loops) {
        REQUIRE(reg.hasCurves());
        // dramatic compression vs the raw boundary (96 raw points)
        REQUIRE(reg.curves.size() < reg.raw.size() / 4);
        int lines = 0, cubics = 0;
        for (const auto& b : reg.curves) (b.isLine ? lines : cubics)++;
        // two straight side seams and two curved arcs (waist, hem)
        REQUIRE(lines >= 2);
        REQUIRE(cubics >= 2);
        // fit accuracy within the simplification tolerance regime
        REQUIRE(reg.curveMaxDeviation <= 0.004 * 1.5);
        // seam-length compatibility budget
        REQUIRE(reg.curveMaxLengthError <= 0.005);
        // whole-loop arc length stays close to the raw boundary length
        double lr = rawLength(reg.raw);
        double lc = curveChainLength(reg.curves);
        REQUIRE(std::abs(lc - lr) / lr < 0.01);
        // closed connected chain
        for (size_t i = 0; i < reg.curves.size(); ++i) {
            const auto& cur = reg.curves[i];
            const auto& next = reg.curves[(i + 1) % reg.curves.size()];
            REQUIRE((cur.p1 - next.p0).norm() < 1e-12);
        }
    }
}

TEST_CASE("curve fitting is deterministic and revertible", "[bezier]") {
    auto l1 = skirtPanelLoops();
    auto l2 = skirtPanelLoops();
    REQUIRE(l1.size() == l2.size());
    for (size_t i = 0; i < l1.size(); ++i) {
        REQUIRE(l1[i].curves.size() == l2[i].curves.size());
        for (size_t j = 0; j < l1[i].curves.size(); ++j) {
            REQUIRE((l1[i].curves[j].p0 - l2[i].curves[j].p0).norm() == 0.0);
            REQUIRE((l1[i].curves[j].c0 - l2[i].curves[j].c0).norm() == 0.0);
            REQUIRE((l1[i].curves[j].c1 - l2[i].curves[j].c1).norm() == 0.0);
            REQUIRE((l1[i].curves[j].p1 - l2[i].curves[j].p1).norm() == 0.0);
        }
        // raw untouched by fitting (revert target intact)
        REQUIRE(l1[i].raw == l2[i].raw);
    }
}

TEST_CASE("smooth closed loop without corners still fits", "[bezier]") {
    // circle: no corners detected -> loop split at two kept points
    std::vector<Vec2> circle;
    for (int i = 0; i < 128; ++i) {
        double a = 2.0 * std::numbers::pi * i / 128;
        circle.emplace_back(0.3 * std::cos(a), 0.3 * std::sin(a));
    }
    RegularizeOptions ro;
    ro.tolerance = 0.002;
    ro.cornerAngleDeg = 60.0;   // ensure nothing counts as a corner
    auto reg = regularizeLoop(circle, ro);
    CurveFitOptions cfo;
    cfo.tolerance = ro.tolerance;
    fitLoopCurves(reg, cfo);
    REQUIRE(reg.hasCurves());
    REQUIRE(reg.curveMaxDeviation <= cfo.tolerance * 1.5);
    double lr = rawLength(circle);
    REQUIRE(std::abs(curveChainLength(reg.curves) - lr) / lr < 0.01);
}
