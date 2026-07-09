#include "core/Procedural.h"
#include "core/SegmentationBaselines.h"

#include <catch2/catch_test_macros.hpp>
#include <unordered_set>

using namespace sf;

namespace {

// Euler characteristic of a chart's face set (disk == 1)
int chartChi(const TriMesh& m, const std::vector<int>& faceChart, int chart) {
    std::unordered_set<int> vs, es;
    int nf = 0;
    for (int f = 0; f < (int)m.F.size(); ++f) {
        if (faceChart[f] != chart) continue;
        ++nf;
        for (int k = 0; k < 3; ++k) {
            vs.insert(m.F[f][k]);
            es.insert(m.faceEdges[f][k]);
        }
    }
    if (nf == 0) return 0;
    return (int)vs.size() - (int)es.size() + nf;
}

} // namespace

TEST_CASE("d-charts covers the skirt with >= 2 disk-topology charts", "[dcharts]") {
    TriMesh m = makeSkirt({});
    auto dc = dchartsSegment(m);
    INFO(dc.log);

    // full coverage
    REQUIRE(dc.faceChart.size() == m.F.size());
    for (int c : dc.faceChart) REQUIRE(c >= 0);

    // a tube cannot be a single disk: enforced topology forces >= 2 charts
    REQUIRE(dc.chartCount >= 2);

    // every chart is a topological disk (flattenable without further cuts)
    for (int c = 0; c < dc.chartCount; ++c) {
        INFO("chart " << c);
        REQUIRE(chartChi(m, dc.faceChart, c) == 1);
    }

    // the frustum is developable: cone-proxy fit must be near-perfect
    REQUIRE(dc.chartRmsFit.size() == (size_t)dc.chartCount);
    for (double e : dc.chartRmsFit) REQUIRE(e < 0.05);
}

TEST_CASE("d-charts is deterministic", "[dcharts]") {
    TriMesh m = makeSkirt({});
    auto d1 = dchartsSegment(m);
    auto d2 = dchartsSegment(m);
    REQUIRE(d1.chartCount == d2.chartCount);
    REQUIRE(d1.faceChart == d2.faceChart);
}

TEST_CASE("d-charts documents its construction-blindness on the skirt", "[dcharts]") {
    // The whole frustum is ONE perfect cone: developability gives no reason
    // to cut at the true side seams, so the wrap-around chart boundary lands
    // at an arbitrary meridian. This test pins down the failure mode the
    // baseline exists to quantify: IoU vs construction ground truth is
    // structurally limited (typically ~0.5), and must NOT be silently high.
    GroundTruth gt;
    TriMesh m = makeSkirt({}, &gt);
    auto dc = dchartsSegment(m);
    double meanIoU = 0;
    for (int g = 0; g < gt.panelCount; ++g) {
        double best = 0;
        for (int c = 0; c < dc.chartCount; ++c) {
            int inter = 0, uni = 0;
            for (size_t f = 0; f < dc.faceChart.size(); ++f) {
                bool inG = gt.faceLabel[f] == g, inC = dc.faceChart[f] == c;
                inter += inG && inC;
                uni += inG || inC;
            }
            if (uni) best = std::max(best, (double)inter / uni);
        }
        meanIoU += best / gt.panelCount;
    }
    INFO("mean IoU " << meanIoU << " (silhouette baseline scores 1.0 here)");
    REQUIRE(meanIoU > 0.1);    // sane segmentation...
    REQUIRE(meanIoU < 0.95);   // ...but provably not construction-aware
}

TEST_CASE("d-charts handles disconnected components", "[dcharts]") {
    // two separate tubes must yield at least one chart per component and
    // never mix components in one chart
    GroundTruth gt;
    TriMesh a = makeSkirt({}, &gt);
    TriMesh m = a;
    int base = (int)m.V.size();
    for (const auto& p : a.V) m.V.push_back(p + Vec3(2.0, 0, 0));
    for (const auto& f : a.F) m.F.push_back({f[0] + base, f[1] + base, f[2] + base});
    m.buildAdjacency();

    auto dc = dchartsSegment(m);
    REQUIRE(dc.chartCount >= 4);   // >= 2 per tube
    int nA = (int)a.F.size();
    for (int c = 0; c < dc.chartCount; ++c) {
        bool inFirst = false, inSecond = false;
        for (int f = 0; f < (int)m.F.size(); ++f)
            if (dc.faceChart[f] == c) (f < nA ? inFirst : inSecond) = true;
        REQUIRE_FALSE((inFirst && inSecond));
    }
}
