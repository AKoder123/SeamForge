#include "core/Procedural.h"
#include "core/Seam.h"
#include "core/Segmentation.h"

#include <catch2/catch_test_macros.hpp>

using namespace sf;

TEST_CASE("assisted proposal finds two side seams on a clean skirt", "[propose]") {
    GroundTruth gt;
    TriMesh m = makeSkirt({}, &gt);
    auto prop = proposeSideSeams(m);
    REQUIRE(prop.seams.size() == 2);
    for (const auto& s : prop.seams) {
        REQUIRE(validateSeamPath(m, s).empty());
        REQUIRE(s.source == Seam::Source::Proposed);
        // proposals must never claim certainty
        REQUIRE(s.confidence > 0.0);
        REQUIRE(s.confidence < 1.0);
        REQUIRE_FALSE(s.evidence.empty());
        // endpoints on the two boundary loops (waist ring 0, hem ring M)
        int N = 64;
        REQUIRE((s.vertices.front() < N || s.vertices.front() >= (int)m.V.size() - N));
        REQUIRE((s.vertices.back() < N || s.vertices.back() >= (int)m.V.size() - N));
    }
}

TEST_CASE("proposed seams segment the skirt with high IoU vs ground truth", "[propose]") {
    GroundTruth gt;
    TriMesh m = makeSkirt({}, &gt);
    auto prop = proposeSideSeams(m);
    REQUIRE(prop.seams.size() == 2);
    auto seg = segmentBySeams(m, prop.seams);
    REQUIRE(seg.panels.size() == 2);

    std::vector<int> pred(m.F.size(), -1);
    for (const auto& p : seg.panels)
        for (int f : p.faces) pred[f] = p.id;
    double meanIoU = 0;
    for (int g = 0; g < gt.panelCount; ++g) {
        double best = 0;
        for (size_t pp = 0; pp < seg.panels.size(); ++pp) {
            int inter = 0, uni = 0;
            for (size_t f = 0; f < pred.size(); ++f) {
                bool inG = gt.faceLabel[f] == g, inP = pred[f] == (int)pp;
                inter += inG && inP;
                uni += inG || inP;
            }
            best = std::max(best, uni ? (double)inter / uni : 0.0);
        }
        meanIoU += best / gt.panelCount;
    }
    INFO("mean IoU " << meanIoU);
    REQUIRE(meanIoU > 0.9);   // silhouette prior should nail the clean skirt
}

TEST_CASE("proposal fails gracefully without garment openings", "[propose]") {
    // closed surface: no boundary loops -> proposer must decline with a log
    TriMesh m;
    m.V = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    m.F = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
    m.buildAdjacency();
    auto prop = proposeSideSeams(m);
    REQUIRE(prop.seams.empty());
    REQUIRE_FALSE(prop.log.empty());
}
