#include "core/Flatten.h"
#include "core/Procedural.h"

#include <catch2/catch_test_macros.hpp>

using namespace sf;

namespace {

SegmentationResult segmentSkirt(const SkirtOptions& opt, GroundTruth& gt) {
    TriMesh m = makeSkirt(opt, &gt);
    std::vector<Seam> seams;
    for (size_t i = 0; i < gt.seamPaths.size(); ++i) {
        Seam s;
        s.id = (int)i;
        s.vertices = gt.seamPaths[i];
        seams.push_back(std::move(s));
    }
    return segmentBySeams(m, seams);
}

} // namespace

// Acceptance thresholds for the two-panel skirt milestone (documented in
// TEST_STRATEGY.md):
//   flipped triangles      == 0
//   ARAP  developable panel: mean angle distortion <= 1.02,
//                            mean |log area ratio| <= 0.02,
//                            boundary length change <= 1%
//   LSCM  developable panel: mean angle distortion <= 1.02
TEST_CASE("ARAP flattens developable skirt panels near-isometrically", "[flatten]") {
    GroundTruth gt;
    auto seg = segmentSkirt({}, gt);
    REQUIRE(seg.panels.size() == 2);
    ARAPFlattener arap;
    for (const auto& p : seg.panels) {
        auto r = arap.flatten(p);
        REQUIRE(r.success);
        REQUIRE(r.UV.size() == p.V.size());
        REQUIRE(r.flippedTriangles == 0);
        auto fd = computeDistortion(p, r.UV);
        auto ds = summarizeDistortion(p, r.UV, fd);
        INFO("panel " << p.id << " meanAngle=" << ds.meanAngleDistortion
                      << " meanAbsLogArea=" << ds.meanAbsLogArea
                      << " boundary=" << ds.boundaryLengthChange);
        REQUIRE(ds.flipped == 0);
        REQUIRE(ds.meanAngleDistortion <= 1.02);
        REQUIRE(ds.meanAbsLogArea <= 0.02);
        REQUIRE(ds.boundaryLengthChange <= 0.01);
        for (const auto& q : r.UV) {
            REQUIRE(std::isfinite(q.x()));
            REQUIRE(std::isfinite(q.y()));
        }
    }
}

TEST_CASE("LSCM flattens skirt panels conformally", "[flatten]") {
    GroundTruth gt;
    auto seg = segmentSkirt({}, gt);
    LSCMFlattener lscm;
    for (const auto& p : seg.panels) {
        auto r = lscm.flatten(p);
        REQUIRE(r.success);
        REQUIRE(r.flippedTriangles == 0);
        auto ds = summarizeDistortion(p, r.UV, computeDistortion(p, r.UV));
        INFO("panel " << p.id << " meanAngle=" << ds.meanAngleDistortion);
        REQUIRE(ds.meanAngleDistortion <= 1.02);
    }
}

TEST_CASE("flattening is reproducible", "[flatten]") {
    GroundTruth gt;
    auto seg = segmentSkirt({}, gt);
    ARAPFlattener arap;
    auto r1 = arap.flatten(seg.panels[0]);
    auto r2 = arap.flatten(seg.panels[0]);
    REQUIRE(r1.success);
    REQUIRE(r2.success);
    for (size_t i = 0; i < r1.UV.size(); ++i)
        REQUIRE((r1.UV[i] - r2.UV[i]).norm() < 1e-12);
}

TEST_CASE("non-developable A-line panel flattens without flips, distortion reported", "[flatten]") {
    SkirtOptions opt;
    opt.bulge = 0.06;
    GroundTruth gt;
    auto seg = segmentSkirt(opt, gt);
    ARAPFlattener arap;
    for (const auto& p : seg.panels) {
        auto r = arap.flatten(p);
        REQUIRE(r.success);
        REQUIRE(r.flippedTriangles == 0);
        auto ds = summarizeDistortion(p, r.UV, computeDistortion(p, r.UV));
        // curved surface cannot flatten isometrically - distortion must be
        // nonzero and honestly reported
        REQUIRE(ds.meanAngleDistortion > 1.0);
        REQUIRE(ds.meanAngleDistortion < 1.2);   // but still moderate
    }
}

TEST_CASE("solver failure is exposed, not hidden", "[flatten]") {
    Panel p;   // empty panel
    ARAPFlattener arap;
    auto r = arap.flatten(p);
    REQUIRE_FALSE(r.success);
    REQUIRE_FALSE(r.message.empty());
}
