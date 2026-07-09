#include "core/Procedural.h"
#include "core/Segmentation.h"

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
} // namespace

TEST_CASE("two side seams cut the skirt into front and back", "[segmentation]") {
    SkirtOptions opt;
    GroundTruth gt;
    TriMesh m = makeSkirt(opt, &gt);
    auto seg = segmentBySeams(m, gtSeams(gt));

    REQUIRE(seg.ok());
    REQUIRE(seg.panels.size() == 2);

    // exact agreement with ground-truth sectors
    for (const auto& p : seg.panels) {
        int label = gt.faceLabel[p.faces.front()];
        for (int f : p.faces) REQUIRE(gt.faceLabel[f] == label);
    }
    // both panels together cover every face exactly once
    size_t total = seg.panels[0].faces.size() + seg.panels[1].faces.size();
    REQUIRE(total == m.F.size());

    // correspondence: every local vertex maps back to a valid original vertex
    for (const auto& p : seg.panels) {
        REQUIRE(p.toOrigV.size() == p.V.size());
        for (size_t lv = 0; lv < p.V.size(); ++lv) {
            int ov = p.toOrigV[lv];
            REQUIRE(ov >= 0);
            REQUIRE(ov < (int)m.V.size());
            REQUIRE((p.V[lv] - m.V[ov]).norm() == 0.0);
        }
    }
    // seam vertices are duplicated: total local verts > original verts
    size_t locals = seg.panels[0].V.size() + seg.panels[1].V.size();
    REQUIRE(locals > m.V.size());
}

TEST_CASE("panel boundary ancestry identifies seams and garment openings", "[segmentation]") {
    SkirtOptions opt;
    GroundTruth gt;
    TriMesh m = makeSkirt(opt, &gt);
    auto seg = segmentBySeams(m, gtSeams(gt));
    REQUIRE(seg.panels.size() == 2);

    for (const auto& p : seg.panels) {
        REQUIRE(p.boundaryLoopsLocal.size() == 1);   // one boundary loop per panel
        std::vector<int> seamSegs, freeSegs;
        for (size_t i = 0; i < p.segments.size(); ++i)
            (p.segments[i].seamId >= 0 ? seamSegs : freeSegs).push_back((int)i);
        REQUIRE(seamSegs.size() == 2);   // two side seams
        REQUIRE(freeSegs.size() == 2);   // waist + hem
        // seam segments span the full seam path
        for (int i : seamSegs)
            REQUIRE(p.segments[i].localVerts.size() == gt.seamPaths[0].size());
    }
}

TEST_CASE("non-separating seam is reported, not hidden", "[segmentation]") {
    SkirtOptions opt;
    GroundTruth gt;
    TriMesh m = makeSkirt(opt, &gt);
    // only half of one seam: dangling endpoint in the interior
    Seam s;
    s.id = 0;
    s.vertices = gt.seamPaths[0];
    s.vertices.resize(gt.seamPaths[0].size() / 2);
    auto seg = segmentBySeams(m, {s});
    REQUIRE_FALSE(seg.ok());
    bool saidNotAnchored = false, saidNotSeparating = false;
    for (const auto& pr : seg.problems) {
        if (pr.find("not anchored") != std::string::npos) saidNotAnchored = true;
        if (pr.find("do not separate") != std::string::npos) saidNotSeparating = true;
    }
    REQUIRE(saidNotAnchored);
    REQUIRE(saidNotSeparating);
}

TEST_CASE("four-panel skirt segments into four panels", "[segmentation]") {
    SkirtOptions opt;
    opt.panels = 4;
    GroundTruth gt;
    TriMesh m = makeSkirt(opt, &gt);
    auto seg = segmentBySeams(m, gtSeams(gt));
    REQUIRE(seg.ok());
    REQUIRE(seg.panels.size() == 4);
    for (const auto& p : seg.panels) {
        int label = gt.faceLabel[p.faces.front()];
        for (int f : p.faces) REQUIRE(gt.faceLabel[f] == label);
    }
}

TEST_CASE("segmentation is deterministic", "[segmentation]") {
    SkirtOptions opt;
    GroundTruth gt;
    TriMesh m = makeSkirt(opt, &gt);
    auto s1 = segmentBySeams(m, gtSeams(gt));
    auto s2 = segmentBySeams(m, gtSeams(gt));
    REQUIRE(s1.panels.size() == s2.panels.size());
    for (size_t i = 0; i < s1.panels.size(); ++i) {
        REQUIRE(s1.panels[i].faces == s2.panels[i].faces);
        REQUIRE(s1.panels[i].toOrigV == s2.panels[i].toOrigV);
    }
}
