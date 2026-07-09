#include "core/Regularize.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

using namespace sf;

namespace {
// noisy rectangle boundary: 4 true corners, dense noisy samples on the edges
std::vector<Vec2> noisyRectangle(double w, double h, int perEdge, double noise) {
    std::vector<Vec2> pts;
    auto edge = [&](Vec2 a, Vec2 b) {
        for (int i = 0; i < perEdge; ++i) {
            double t = (double)i / perEdge;
            Vec2 p = a + t * (b - a);
            // deterministic pseudo-noise perpendicular to the edge
            Vec2 dir = (b - a).normalized();
            Vec2 n(-dir.y(), dir.x());
            p += n * noise * std::sin(t * 40.0);
            pts.push_back(p);
        }
    };
    edge({0, 0}, {w, 0});
    edge({w, 0}, {w, h});
    edge({w, h}, {0, h});
    edge({0, h}, {0, 0});
    return pts;
}
} // namespace

TEST_CASE("simplification stays within tolerance and reduces points", "[regularize]") {
    auto loop = noisyRectangle(0.4, 0.6, 60, 0.0008);
    RegularizeOptions opt;
    opt.tolerance = 0.004;
    auto reg = regularizeLoop(loop, opt);
    REQUIRE(reg.simplified.size() < loop.size() / 4);
    REQUIRE(reg.maxDeviation <= opt.tolerance * 1.5);   // deviation is measured & bounded
    REQUIRE(reg.raw.size() == loop.size());             // raw kept for revert
}

TEST_CASE("rectangle corners are preserved and detected", "[regularize]") {
    auto loop = noisyRectangle(0.4, 0.6, 60, 0.0005);
    RegularizeOptions opt;
    opt.tolerance = 0.003;
    auto reg = regularizeLoop(loop, opt);
    // each true corner must have a kept point nearby flagged as a corner
    std::vector<Vec2> corners = {{0, 0}, {0.4, 0}, {0.4, 0.6}, {0, 0.6}};
    for (const auto& c : corners) {
        bool found = false;
        for (size_t i = 0; i < reg.simplified.size(); ++i)
            if (reg.isCorner[i] && (reg.simplified[i] - c).norm() < 0.02) found = true;
        INFO("corner " << c.x() << "," << c.y());
        REQUIRE(found);
    }
}

TEST_CASE("straight runs are identified", "[regularize]") {
    // clean rectangle: all four edges should be flagged straight
    auto loop = noisyRectangle(0.4, 0.6, 40, 0.0);
    RegularizeOptions opt;
    opt.tolerance = 0.004;
    auto reg = regularizeLoop(loop, opt);
    int straight = 0;
    for (bool s : reg.isStraight) straight += s ? 1 : 0;
    REQUIRE(straight >= 4);
}
