#include "core/Flatten.h"
#include "core/Procedural.h"
#include "core/Relations.h"
#include "core/Resimulate.h"

#include <catch2/catch_test_macros.hpp>

using namespace sf;

namespace {

// full skirt reconstruction packaged as a project (the resim input)
Project skirtProject(const SkirtOptions& opt = {}) {
    GroundTruth gt;
    Project proj;
    proj.mesh = makeSkirt(opt, &gt);
    for (size_t i = 0; i < gt.seamPaths.size(); ++i) {
        Seam s;
        s.id = (int)i;
        s.vertices = gt.seamPaths[i];
        proj.seams.push_back(std::move(s));
    }
    auto seg = segmentBySeams(proj.mesh, proj.seams);
    ARAPFlattener arap;
    for (auto& p : seg.panels) p.UV = arap.flatten(p).UV;
    proj.panels = seg.panels;
    proj.relations = deriveSeamRelations(proj.mesh, proj.seams, seg);
    return proj;
}

} // namespace

TEST_CASE("consistent skirt pattern reconstructs to the source shape", "[resim]") {
    Project proj = skirtProject();
    auto m = resimulateValidate(proj);
    REQUIRE(m.success);
    INFO("drift rms " << m.driftRms << " m (" << m.driftRmsRel * 100
                      << "%), chamfer " << m.chamfer << ", normals "
                      << m.meanNormalDeviationDeg << " deg, IoU "
                      << m.silhouetteIoU << ", seam gap " << m.maxSeamGap);
    // acceptance: equilibrium stays within 1% of the bbox diagonal
    REQUIRE(m.driftRmsRel < 0.01);
    REQUIRE(m.silhouetteIoU > 0.95);
    REQUIRE(m.meanNormalDeviationDeg < 5.0);
    // seams must close (pattern seam lengths are compatible)
    REQUIRE(m.maxSeamGap < 0.005);
}

TEST_CASE("a corrupted pattern is detected, not accepted", "[resim]") {
    Project proj = skirtProject();
    auto good = resimulateValidate(proj);
    REQUIRE(good.success);

    ResimOptions bad;
    bad.corruptPanel0Scale = 1.06;   // front panel pattern 6% too large
    auto corrupt = resimulateValidate(proj, bad);
    REQUIRE(corrupt.success);
    INFO("good drift " << good.driftRmsRel << " vs corrupt " << corrupt.driftRmsRel);
    // discrimination: the inconsistent pattern must drift far more
    // (measured: ~5e-12 consistent vs ~8.3e-3 with a 6% panel-scale error)
    REQUIRE(corrupt.driftRmsRel > 100.0 * good.driftRmsRel);
    REQUIRE(corrupt.driftRmsRel > 0.005);
    // and the seams no longer close cleanly against the oversized panel
    REQUIRE(corrupt.driftMax > good.driftMax);
}

TEST_CASE("resimulation works on the non-developable A-line skirt", "[resim]") {
    SkirtOptions opt;
    opt.bulge = 0.06;
    Project proj = skirtProject(opt);
    auto m = resimulateValidate(proj);
    REQUIRE(m.success);
    INFO("drift rms rel " << m.driftRmsRel << ", normals "
                          << m.meanNormalDeviationDeg << " deg");
    // ARAP absorbs the non-developability into bounded distortion; the
    // pattern is still metrically close, so reconstruction stays near the
    // source - but a wider budget than the developable case is honest
    REQUIRE(m.driftRmsRel < 0.03);
    REQUIRE(m.silhouetteIoU > 0.9);
}

TEST_CASE("resimulation is deterministic and reports missing inputs", "[resim]") {
    Project proj = skirtProject();
    auto m1 = resimulateValidate(proj);
    auto m2 = resimulateValidate(proj);
    REQUIRE(m1.driftRms == m2.driftRms);
    REQUIRE(m1.silhouetteIoU == m2.silhouetteIoU);

    Project empty;
    auto bad = resimulateValidate(empty);
    REQUIRE_FALSE(bad.success);
    REQUIRE_FALSE(bad.message.empty());

    Project noUV = skirtProject();
    for (auto& p : noUV.panels) p.UV.clear();
    auto bad2 = resimulateValidate(noUV);
    REQUIRE_FALSE(bad2.success);
}
