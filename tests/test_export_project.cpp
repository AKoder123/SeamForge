#include "core/Export.h"
#include "core/Flatten.h"
#include "core/Procedural.h"
#include "core/Project.h"
#include "core/Regularize.h"
#include "core/Relations.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace sf;
namespace fs = std::filesystem;

namespace {
struct Pipeline {
    TriMesh mesh;
    std::vector<Seam> seams;
    SegmentationResult seg;
    std::vector<SeamRelation> rels;
    std::vector<std::vector<RegularizedLoop>> regularized;
    Pipeline() {
        GroundTruth gt;
        mesh = makeSkirt({}, &gt);
        for (size_t i = 0; i < gt.seamPaths.size(); ++i) {
            Seam s;
            s.id = (int)i;
            s.vertices = gt.seamPaths[i];
            seams.push_back(std::move(s));
        }
        seg = segmentBySeams(mesh, seams);
        ARAPFlattener arap;
        for (auto& p : seg.panels) p.UV = arap.flatten(p).UV;
        rels = deriveSeamRelations(mesh, seams, seg);
        updateRelationLengths(rels, seg.panels);
        RegularizeOptions ro;
        ro.tolerance = 0.004 * mesh.bbox().diagonal().norm();
        for (const auto& p : seg.panels) {
            std::vector<RegularizedLoop> per;
            TriMesh pm = p.toTriMesh();
            CurveFitOptions cfo;
            cfo.tolerance = ro.tolerance;
            for (const auto& loop : pm.boundaryLoops()) {
                std::vector<Vec2> pts;
                for (int v : loop) pts.push_back(p.UV[v]);
                auto reg = regularizeLoop(pts, ro);
                fitLoopCurves(reg, cfo);
                per.push_back(std::move(reg));
            }
            regularized.push_back(std::move(per));
        }
    }
};
} // namespace

TEST_CASE("SVG export is complete and deterministic", "[export]") {
    Pipeline pl;
    std::string svg1 = renderSvg(pl.seg.panels, pl.regularized, pl.rels);
    std::string svg2 = renderSvg(pl.seg.panels, pl.regularized, pl.rels);
    REQUIRE(svg1 == svg2);   // reproducible output
    REQUIRE(svg1.find("<svg") != std::string::npos);
    REQUIRE(svg1.find("panel-0") != std::string::npos);
    REQUIRE(svg1.find("panel-1") != std::string::npos);
    REQUIRE(svg1.find("seam-relations") != std::string::npos);
    bool hasLabel = svg1.find("front") != std::string::npos ||
                    svg1.find("unknown") != std::string::npos;
    REQUIRE(hasLabel);
    // fitted Bezier outline exported as an SVG path with cubic commands
    REQUIRE(svg1.find("<path d=\"M ") != std::string::npos);
    REQUIRE(svg1.find(" C ") != std::string::npos);
}

TEST_CASE("DXF export writes valid minimal R12", "[export]") {
    Pipeline pl;
    fs::path tmp = fs::temp_directory_path() / "seamforge_test.dxf";
    std::string err;
    REQUIRE(writeDxf(tmp.string(), pl.seg.panels, pl.regularized, {}, &err));
    std::ifstream f(tmp);
    std::string content((std::istreambuf_iterator<char>(f)), {});
    REQUIRE(content.find("POLYLINE") != std::string::npos);
    REQUIRE(content.find("EOF") != std::string::npos);
    fs::remove(tmp);
}

TEST_CASE("project save/load round-trip is lossless", "[project]") {
    Pipeline pl;
    Project proj;
    proj.sourcePath = "procedural://skirt_simple";
    proj.mesh = pl.mesh;
    proj.seams = pl.seams;
    proj.panels = pl.seg.panels;
    proj.relations = pl.rels;
    proj.regularized = pl.regularized;
    proj.flattenerName = "arap";

    fs::path tmp = fs::temp_directory_path() / "seamforge_test.sfrproj";
    std::string err;
    REQUIRE(proj.save(tmp.string(), &err));

    Project loaded;
    REQUIRE(Project::load(tmp.string(), loaded, &err));
    REQUIRE(loaded.mesh.V.size() == pl.mesh.V.size());
    REQUIRE(loaded.mesh.F == pl.mesh.F);
    REQUIRE(loaded.seams.size() == pl.seams.size());
    REQUIRE(loaded.panels.size() == pl.seg.panels.size());
    for (size_t i = 0; i < loaded.panels.size(); ++i) {
        REQUIRE(loaded.panels[i].toOrigV == pl.seg.panels[i].toOrigV);
        REQUIRE(loaded.panels[i].UV.size() == pl.seg.panels[i].UV.size());
        REQUIRE(loaded.panels[i].segments.size() == pl.seg.panels[i].segments.size());
    }
    REQUIRE(loaded.relations.size() == pl.rels.size());
    for (size_t i = 0; i < loaded.relations.size(); ++i) {
        REQUIRE(loaded.relations[i].a.localVerts == pl.rels[i].a.localVerts);
        REQUIRE(loaded.relations[i].b.localVerts == pl.rels[i].b.localVerts);
    }
    // fitted curves survive the round trip
    REQUIRE(loaded.regularized.size() == pl.regularized.size());
    for (size_t i = 0; i < loaded.regularized.size(); ++i)
        for (size_t j = 0; j < loaded.regularized[i].size(); ++j) {
            const auto& a = loaded.regularized[i][j];
            const auto& b = pl.regularized[i][j];
            REQUIRE(a.curves.size() == b.curves.size());
            for (size_t k = 0; k < a.curves.size(); ++k) {
                REQUIRE(a.curves[k].isLine == b.curves[k].isLine);
                REQUIRE((a.curves[k].c0 - b.curves[k].c0).norm() == 0.0);
            }
        }
    // idempotent: serialising the loaded project matches the original JSON
    REQUIRE(loaded.toJson() == proj.toJson());
    fs::remove(tmp);
}

TEST_CASE("loading garbage fails with a message", "[project]") {
    fs::path tmp = fs::temp_directory_path() / "seamforge_garbage.sfrproj";
    std::ofstream(tmp.string()) << "{\"format\": \"something-else\"}";
    Project loaded;
    std::string err;
    REQUIRE_FALSE(Project::load(tmp.string(), loaded, &err));
    REQUIRE_FALSE(err.empty());
    fs::remove(tmp);
}
