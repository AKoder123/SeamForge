#include "core/Flatten.h"
#include "core/Matching.h"
#include "core/Procedural.h"

#include <catch2/catch_test_macros.hpp>

using namespace sf;

namespace {

// The simple skirt cut into front/back, then re-imported as one mesh with
// two disconnected components: the "pre-cut garment" scenario, where no
// cut ancestry exists and matching must work from geometry alone.
TriMesh precutSkirt() {
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
    TriMesh merged;
    for (const auto& p : seg.panels) {
        int base = (int)merged.V.size();
        merged.V.insert(merged.V.end(), p.V.begin(), p.V.end());
        for (const auto& f : p.F)
            merged.F.push_back({f[0] + base, f[1] + base, f[2] + base});
    }
    merged.buildAdjacency();
    return merged;
}

} // namespace

TEST_CASE("pre-cut skirt: side seams matched, openings left unmatched", "[matching]") {
    TriMesh m = precutSkirt();
    auto seg = segmentBySeams(m, {});           // components -> panels, no cuts
    REQUIRE(seg.panels.size() == 2);

    auto match = proposeBoundaryMatches(seg.panels);
    INFO(match.log);

    // exactly the two side seams
    REQUIRE(match.relations.size() == 2);
    REQUIRE(match.seams.size() == 2);
    for (const auto& r : match.relations) {
        REQUIRE(r.source == "boundary-matching");
        REQUIRE(r.a.panelId != r.b.panelId);
        // proposals never claim user-confirmed certainty
        REQUIRE(r.confidence > 0.0);
        REQUIRE(r.confidence < 1.0);
        // matched sides coincide in 3D: co-oriented endpoints must touch
        const Panel& pa = seg.panels[r.a.panelId];
        const Panel& pb = seg.panels[r.b.panelId];
        Vec3 aS = pa.V[r.a.localVerts.front()], aE = pa.V[r.a.localVerts.back()];
        Vec3 bS = pb.V[r.b.localVerts.front()], bE = pb.V[r.b.localVerts.back()];
        REQUIRE((aS - bS).norm() < 1e-9);
        REQUIRE((aE - bE).norm() < 1e-9);
        REQUIRE(r.length3d > 0);
    }
    // waist front/back + hem front/back stay unmatched (reported, not hidden)
    REQUIRE(match.unmatchedArcs.size() == 4);

    // seam geometry is valid on the merged mesh
    for (const auto& s : match.seams) {
        REQUIRE(s.source == Seam::Source::MeshBoundary);
        REQUIRE(validateSeamPath(m, s).empty());
    }
}

TEST_CASE("matched seam sides agree in 2D length after flattening", "[matching]") {
    TriMesh m = precutSkirt();
    auto seg = segmentBySeams(m, {});
    auto match = proposeBoundaryMatches(seg.panels);
    REQUIRE(match.relations.size() == 2);
    ARAPFlattener arap;
    for (auto& p : seg.panels) {
        auto fr = arap.flatten(p);
        REQUIRE(fr.success);
        p.UV = fr.UV;
    }
    updateRelationLengths(match.relations, seg.panels);
    for (const auto& r : match.relations) {
        INFO("seam " << r.seamId << " mismatch " << r.lengthMismatch2d);
        REQUIRE(r.lengthMismatch2d < 0.02);
    }
}

TEST_CASE("boundary matching is deterministic", "[matching]") {
    TriMesh m = precutSkirt();
    auto seg = segmentBySeams(m, {});
    auto m1 = proposeBoundaryMatches(seg.panels);
    auto m2 = proposeBoundaryMatches(seg.panels);
    REQUIRE(m1.relations.size() == m2.relations.size());
    for (size_t i = 0; i < m1.relations.size(); ++i) {
        REQUIRE(m1.relations[i].a.localVerts == m2.relations[i].a.localVerts);
        REQUIRE(m1.relations[i].b.localVerts == m2.relations[i].b.localVerts);
        REQUIRE(m1.relations[i].confidence == m2.relations[i].confidence);
    }
    REQUIRE(m1.unmatchedArcs == m2.unmatchedArcs);
}

TEST_CASE("matching declines gracefully with fewer than two panels", "[matching]") {
    GroundTruth gt;
    TriMesh m = makeSkirt({}, &gt);
    auto seg = segmentBySeams(m, {});   // one component, no seams
    REQUIRE(seg.panels.size() == 1);
    auto match = proposeBoundaryMatches(seg.panels);
    REQUIRE(match.relations.empty());
    REQUIRE_FALSE(match.log.empty());
}

TEST_CASE("panels with cut ancestry only offer their free boundaries", "[matching]") {
    // Cut the skirt ourselves: side boundaries carry seam ancestry and must
    // NOT be re-matched; only waist/hem (free) arcs are candidates, and
    // those genuinely do not sew to each other -> no matches.
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
    REQUIRE(seg.panels.size() == 2);
    auto match = proposeBoundaryMatches(seg.panels);
    INFO(match.log);
    REQUIRE(match.relations.empty());
    REQUIRE(match.unmatchedArcs.size() == 4);   // waist x2, hem x2
}
