#include "core/Curvature.h"
#include "core/Flatten.h"
#include "core/Procedural.h"
#include "core/Relations.h"
#include "core/Validation.h"

#include <catch2/catch_test_macros.hpp>

using namespace sf;

namespace {

std::vector<Seam> gtSeams(const GroundTruth& gt) {
    std::vector<Seam> seams;
    for (size_t i = 0; i < gt.seamPaths.size(); ++i) {
        Seam s;
        s.id = (int)i;
        s.vertices = gt.seamPaths[i];
        seams.push_back(std::move(s));
    }
    return seams;
}

// shared checks for a two-sheet garment: validity, exact segmentation into
// front/back, flattenability, deterministic pairing of every seam
void checkTwoSheetGarment(TriMesh m, const GroundTruth& gt,
                          size_t expectedLoops, size_t expectedSeams) {
    auto rep = validateAndRepair(m);
    INFO(rep.toText());
    REQUIRE_FALSE(rep.hasErrors());
    REQUIRE(m.numNonManifoldEdges() == 0);
    REQUIRE(m.boundaryLoops().size() == expectedLoops);
    REQUIRE(gt.seamPaths.size() == expectedSeams);

    for (const auto& path : gt.seamPaths) {
        Seam s;
        s.vertices = path;
        REQUIRE(validateSeamPath(m, s).empty());
    }

    auto seams = gtSeams(gt);
    auto seg = segmentBySeams(m, seams);
    INFO([&] {
        std::string all;
        for (const auto& p : seg.problems) all += p + "\n";
        return all;
    }());
    REQUIRE(seg.ok());
    REQUIRE((int)seg.panels.size() == gt.panelCount);

    // exact agreement with ground-truth sheet labels
    for (const auto& p : seg.panels) {
        int label = gt.faceLabel[p.faces.front()];
        for (int f : p.faces) REQUIRE(gt.faceLabel[f] == label);
    }

    // every panel flattens without flips
    ARAPFlattener arap;
    for (auto& p : seg.panels) {
        auto fr = arap.flatten(p);
        INFO("panel " << p.id << ": " << fr.message);
        REQUIRE(fr.success);
        REQUIRE(fr.flippedTriangles == 0);
        p.UV = fr.UV;
    }

    // every seam pairs the two sheets deterministically and agrees in length
    auto rels = deriveSeamRelations(m, seams, seg);
    updateRelationLengths(rels, seg.panels);
    REQUIRE(rels.size() == expectedSeams);
    for (const auto& r : rels) {
        INFO("seam " << r.seamId << " note: " << r.note);
        REQUIRE(r.confidence == 1.0);
        REQUIRE(r.a.panelId != r.b.panelId);
        REQUIRE(r.lengthMismatch2d < 0.05);
    }
}

} // namespace

TEST_CASE("boxy tee: 2 panels, 4 openings, 4 seams, full pipeline", "[garments]") {
    GroundTruth gt;
    TriMesh m = makeBoxyTee({}, &gt);
    // neck + waist + two cuffs
    checkTwoSheetGarment(std::move(m), gt, 4, 4);
}

TEST_CASE("flat trousers: 2 panels, 3 openings, 3 seams, full pipeline", "[garments]") {
    GroundTruth gt;
    TriMesh m = makeFlatTrousers({}, &gt);
    // waist + two ankles
    checkTwoSheetGarment(std::move(m), gt, 3, 3);
}

TEST_CASE("dart skirt: creases carry curvature evidence, side seams still work",
          "[garments]") {
    SkirtOptions opt;
    opt.darts = 2;
    GroundTruth gt;
    TriMesh m = makeSkirt(opt, &gt);
    REQUIRE(gt.dartPaths.size() == 2);

    auto rep = validateAndRepair(m);
    REQUIRE_FALSE(rep.hasErrors());

    // dart creases must stand out against the smooth surface: mean dihedral
    // deviation along dart edges well above the mesh average (this is the
    // evidence a curvature-based seam proposer would use)
    CurvatureField cf = computeCurvature(m);
    double meshMean = 0;
    int n = 0;
    for (size_t e = 0; e < m.edges.size(); ++e)
        if (m.edges[e].faces.size() == 2) { meshMean += cf.dihedral[e]; ++n; }
    meshMean /= n;
    for (const auto& path : gt.dartPaths) {
        double dartMean = 0;
        int ne = 0;
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            int e = m.edgeBetween(path[i], path[i + 1]);
            REQUIRE(e >= 0);
            dartMean += cf.dihedral[e];
            ++ne;
        }
        dartMean /= ne;
        INFO("dart mean dihedral " << dartMean << " vs mesh mean " << meshMean);
        REQUIRE(dartMean > 3.0 * meshMean);
    }

    // the standard side-seam workflow is unaffected by darts
    auto seg = segmentBySeams(m, gtSeams(gt));
    REQUIRE(seg.ok());
    REQUIRE(seg.panels.size() == 2);
    ARAPFlattener arap;
    for (const auto& p : seg.panels) {
        auto fr = arap.flatten(p);
        REQUIRE(fr.success);
        REQUIRE(fr.flippedTriangles == 0);
        // darts break developability: distortion must be reported, not hidden
        auto ds = summarizeDistortion(p, fr.UV, computeDistortion(p, fr.UV));
        REQUIRE(ds.meanAngleDistortion > 1.0);
    }

    // a dart used as a cutting seam has an unanchored interior endpoint:
    // segmentation must refuse with a report (documented gap, LIMITATIONS #4)
    Seam dart;
    dart.id = 99;
    dart.vertices = gt.dartPaths[0];
    auto segDart = segmentBySeams(m, {dart});
    REQUIRE_FALSE(segDart.ok());
}

TEST_CASE("two-sheet generators are deterministic", "[garments]") {
    GroundTruth g1, g2;
    TriMesh a = makeBoxyTee({}, &g1);
    TriMesh b = makeBoxyTee({}, &g2);
    REQUIRE(a.V.size() == b.V.size());
    REQUIRE(a.F == b.F);
    REQUIRE(g1.seamPaths == g2.seamPaths);
    REQUIRE(g1.faceLabel == g2.faceLabel);
}
