#include "core/Flatten.h"
#include "core/Procedural.h"
#include "core/Relations.h"

#include <catch2/catch_test_macros.hpp>

using namespace sf;

namespace {
struct Fixture {
    TriMesh mesh;
    std::vector<Seam> seams;
    SegmentationResult seg;
    Fixture(SkirtOptions opt = {}) {
        GroundTruth gt;
        mesh = makeSkirt(opt, &gt);
        for (size_t i = 0; i < gt.seamPaths.size(); ++i) {
            Seam s;
            s.id = (int)i;
            s.vertices = gt.seamPaths[i];
            seams.push_back(std::move(s));
        }
        seg = segmentBySeams(mesh, seams);
    }
};
} // namespace

TEST_CASE("side seams pair front and back deterministically", "[relations]") {
    Fixture fx;
    auto rels = deriveSeamRelations(fx.mesh, fx.seams, fx.seg);
    REQUIRE(rels.size() == 2);
    for (const auto& r : rels) {
        REQUIRE(r.confidence == 1.0);
        REQUIRE(r.source == "cut-ancestry");
        REQUIRE(r.note.empty());
        REQUIRE(r.a.panelId != r.b.panelId);
        // vertex-exact: both sides walk the original seam path
        REQUIRE(r.a.localVerts.size() == fx.seams[r.seamId].vertices.size());
        REQUIRE(r.b.localVerts.size() == r.a.localVerts.size());
        // corresponding points coincide in 3D (they were the same point)
        const Panel& pa = fx.seg.panels[r.a.panelId];
        const Panel& pb = fx.seg.panels[r.b.panelId];
        for (size_t i = 0; i < r.a.localVerts.size(); ++i)
            REQUIRE((pa.V[r.a.localVerts[i]] - pb.V[r.b.localVerts[i]]).norm() == 0.0);
        REQUIRE(r.length3d > 0);
    }
    // determinism
    auto rels2 = deriveSeamRelations(fx.mesh, fx.seams, fx.seg);
    for (size_t i = 0; i < rels.size(); ++i) {
        REQUIRE(rels[i].a.localVerts == rels2[i].a.localVerts);
        REQUIRE(rels[i].b.localVerts == rels2[i].b.localVerts);
    }
}

TEST_CASE("2D seam lengths match across panels after ARAP", "[relations]") {
    Fixture fx;
    ARAPFlattener arap;
    for (auto& p : fx.seg.panels) {
        auto r = arap.flatten(p);
        REQUIRE(r.success);
        p.UV = r.UV;
    }
    auto rels = deriveSeamRelations(fx.mesh, fx.seams, fx.seg);
    updateRelationLengths(rels, fx.seg.panels);
    for (const auto& r : rels) {
        INFO("seam " << r.seamId << " mismatch " << r.lengthMismatch2d);
        REQUIRE(r.lengthMismatch2d < 0.02);   // <2% seam length disagreement
    }
}

TEST_CASE("non-separating seam relation is flagged ambiguous", "[relations]") {
    GroundTruth gt;
    SkirtOptions opt;
    TriMesh mesh = makeSkirt(opt, &gt);
    // one seam only: cuts the tube open into a single panel (both sides of
    // the seam belong to the same panel)
    Seam s;
    s.id = 0;
    s.vertices = gt.seamPaths[0];
    auto seg = segmentBySeams(mesh, {s});
    auto rels = deriveSeamRelations(mesh, {s}, seg);
    REQUIRE(rels.size() == 1);
    REQUIRE(rels[0].confidence < 1.0);
    REQUIRE_FALSE(rels[0].note.empty());
}
