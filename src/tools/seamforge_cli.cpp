// seamforge-cli: headless driver for the reverse-reconstruction pipeline.
//
// Subcommands:
//   validate  --mesh M                      validate & report
//   propose   --mesh M --out seams.json     assisted side-seam proposal
//   pipeline  --mesh M --seams S.json --out DIR [--flattener lscm|arap]
//             [--project P.sfrproj] [--heatmap]
//             full two-panel workflow: segment, flatten, distortion, seam
//             pairing, regularise, SVG/DXF export, project save
//   auto      --mesh M --truth GT.json --out DIR [--baseline silhouette|dcharts]
//             automatic segmentation baseline vs ground truth (IoU)
//   match     --mesh M --out DIR [--flattener lscm|arap] [--project P]
//             pre-cut garment (separate panel meshes as disconnected
//             components): match panel boundaries into seam proposals,
//             then flatten and export
//   roundtrip --project P.sfrproj           load & re-save, verify identical
//
// Seam JSON format: {"seams": [{"vertices": [i0, i1, ...]}, ...]}

#include "core/Export.h"
#include "core/Flatten.h"
#include "core/Io.h"
#include "core/Matching.h"
#include "core/Project.h"
#include "core/Regularize.h"
#include "core/Relations.h"
#include "core/Seam.h"
#include "core/Segmentation.h"
#include "core/SegmentationBaselines.h"
#include "core/Validation.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::map<std::string, std::string> parseArgs(int argc, char** argv, int from) {
    std::map<std::string, std::string> a;
    for (int i = from; i < argc; ++i) {
        std::string k = argv[i];
        if (k.rfind("--", 0) != 0) continue;
        k = k.substr(2);
        if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0)
            a[k] = argv[++i];
        else
            a[k] = "1";
    }
    return a;
}

int fail(const std::string& msg) {
    std::cerr << "error: " << msg << "\n";
    return 1;
}

bool loadAndValidate(const std::string& path, sf::TriMesh& mesh,
                     sf::ValidationReport& rep, std::string& err) {
    auto importer = sf::makeImporterFor(path);
    auto res = importer->load(path);
    if (!res.success) {
        err = res.message;
        return false;
    }
    mesh = std::move(res.mesh);
    rep = sf::validateAndRepair(mesh);
    return true;
}

std::vector<sf::Seam> loadSeams(const std::string& path, std::string& err) {
    std::vector<sf::Seam> seams;
    std::ifstream f(path);
    if (!f) {
        err = "cannot open " + path;
        return seams;
    }
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        err = e.what();
        return seams;
    }
    int id = 0;
    for (const auto& js : j.value("seams", json::array())) {
        sf::Seam s;
        s.id = js.value("id", id);
        s.vertices = js.value("vertices", std::vector<int>{});
        s.source = sf::Seam::Source::Manual;
        s.confidence = js.value("confidence", 1.0);
        seams.push_back(std::move(s));
        ++id;
    }
    return seams;
}

json distortionJson(const sf::DistortionSummary& d) {
    return {{"flipped", d.flipped},
            {"meanAngleDistortion", d.meanAngleDistortion},
            {"maxAngleDistortion", d.maxAngleDistortion},
            {"meanAbsLogArea", d.meanAbsLogArea},
            {"minAreaRatio", d.minAreaRatio},
            {"maxAreaRatio", d.maxAreaRatio},
            {"maxStretch", d.maxStretch},
            {"maxCompression", d.maxCompression},
            {"boundaryLengthChange", d.boundaryLengthChange}};
}

int cmdValidate(const std::map<std::string, std::string>& a) {
    auto it = a.find("mesh");
    if (it == a.end()) return fail("--mesh required");
    sf::TriMesh mesh;
    sf::ValidationReport rep;
    std::string err;
    if (!loadAndValidate(it->second, mesh, rep, err)) return fail(err);
    std::cout << "vertices: " << mesh.V.size() << "  faces: " << mesh.F.size()
              << "  boundary loops: " << mesh.boundaryLoops().size() << "\n"
              << rep.toText()
              << (rep.flattenable() ? "mesh is suitable for reconstruction\n"
                                    : "mesh has blocking errors\n");
    return rep.flattenable() ? 0 : 2;
}

int cmdPropose(const std::map<std::string, std::string>& a) {
    if (!a.count("mesh") || !a.count("out")) return fail("--mesh and --out required");
    sf::TriMesh mesh;
    sf::ValidationReport rep;
    std::string err;
    if (!loadAndValidate(a.at("mesh"), mesh, rep, err)) return fail(err);
    auto prop = sf::proposeSideSeams(mesh);
    std::cerr << prop.log;
    json j;
    j["seams"] = json::array();
    for (const auto& s : prop.seams)
        j["seams"].push_back({{"id", s.id},
                              {"vertices", s.vertices},
                              {"confidence", s.confidence},
                              {"evidence", s.evidence}});
    std::ofstream f(a.at("out"));
    f << j.dump(1) << "\n";
    std::cout << "proposed " << prop.seams.size() << " seam(s) -> " << a.at("out") << "\n";
    return prop.seams.empty() ? 2 : 0;
}

int runPipeline(sf::TriMesh& mesh, std::vector<sf::Seam> seams,
                const std::string& flattenerName, const fs::path& outDir,
                bool heatmap, const std::string& projectPath,
                const std::string& sourcePath, const std::string& validationText,
                json& metrics) {
    fs::create_directories(outDir);

    auto seg = sf::segmentBySeams(mesh, seams);
    for (const auto& p : seg.problems) std::cerr << "segmentation: " << p << "\n";
    if (!seg.ok() && seg.panels.size() < 2)
        return fail("segmentation failed");
    metrics["panelCount"] = seg.panels.size();

    // milestone labelling heuristic for a two-panel skirt: panel containing
    // the face with the most +Z centroid = front
    if (seg.panels.size() == 2) {
        auto meanZ = [&](const sf::Panel& p) {
            double z = 0;
            for (const auto& v : p.V) z += v.z();
            return z / (double)p.V.size();
        };
        int front = meanZ(seg.panels[0]) > meanZ(seg.panels[1]) ? 0 : 1;
        seg.panels[front].label = "front";
        seg.panels[1 - front].label = "back";
    }

    auto flattener = sf::makeFlattener(flattenerName);
    if (!flattener) return fail("unknown flattener " + flattenerName);

    json panelsJson = json::array();
    for (auto& p : seg.panels) {
        auto fr = flattener->flatten(p);
        if (!fr.success) return fail("flatten panel " + std::to_string(p.id) + ": " + fr.message);
        p.UV = fr.UV;
        auto fd = sf::computeDistortion(p, p.UV);
        auto ds = sf::summarizeDistortion(p, p.UV, fd);
        std::cout << "panel " << p.id << " (" << p.label << "): " << p.F.size()
                  << " tris, flipped=" << ds.flipped
                  << ", mean angle dist=" << ds.meanAngleDistortion
                  << ", mean |log area|=" << ds.meanAbsLogArea
                  << ", boundary len change=" << ds.boundaryLengthChange * 100 << "%\n";
        panelsJson.push_back({{"id", p.id},
                              {"label", p.label},
                              {"faces", p.F.size()},
                              {"distortion", distortionJson(ds)},
                              {"iterations", fr.iterations}});
    }
    metrics["panels"] = panelsJson;

    auto rels = sf::deriveSeamRelations(mesh, seams, seg);
    sf::updateRelationLengths(rels, seg.panels);
    json relsJson = json::array();
    for (const auto& r : rels) {
        std::cout << "seam " << r.seamId << ": panel " << r.a.panelId << " <-> panel "
                  << r.b.panelId << ", 3D length " << r.length3d << ", 2D mismatch "
                  << r.lengthMismatch2d * 100 << "%, confidence " << r.confidence
                  << (r.note.empty() ? "" : (" [" + r.note + "]")) << "\n";
        relsJson.push_back({{"seamId", r.seamId},
                            {"panelA", r.a.panelId},
                            {"panelB", r.b.panelId},
                            {"length3d", r.length3d},
                            {"lengthMismatch2d", r.lengthMismatch2d},
                            {"confidence", r.confidence},
                            {"note", r.note}});
    }
    metrics["relations"] = relsJson;

    // regularise boundaries (tolerance: 0.4% of bbox diag of the mesh)
    double tol = 0.004 * mesh.bbox().diagonal().norm();
    sf::RegularizeOptions ro;
    ro.tolerance = tol;
    std::vector<std::vector<sf::RegularizedLoop>> regularized;
    for (const auto& p : seg.panels) {
        std::vector<sf::RegularizedLoop> per;
        sf::TriMesh pm = p.toTriMesh();
        for (const auto& loop : pm.boundaryLoops()) {
            std::vector<sf::Vec2> pts;
            for (int v : loop) pts.push_back(p.UV[v]);
            auto reg = sf::regularizeLoop(pts, ro);
            sf::CurveFitOptions cfo;
            cfo.tolerance = ro.tolerance;
            sf::fitLoopCurves(reg, cfo);
            std::cout << "panel " << p.id << " boundary: " << reg.simplified.size()
                      << " pts -> " << reg.curves.size() << " curve segments"
                      << ", fit dev " << reg.curveMaxDeviation
                      << ", len err " << reg.curveMaxLengthError * 100 << "%\n";
            per.push_back(std::move(reg));
        }
        regularized.push_back(std::move(per));
    }

    sf::ExportOptions eo;
    eo.drawHeatmap = heatmap;
    std::string err;
    if (!sf::writeSvg((outDir / "pattern.svg").string(), seg.panels, regularized, rels, eo, &err))
        return fail("svg: " + err);
    if (!sf::writeDxf((outDir / "pattern.dxf").string(), seg.panels, regularized, eo, &err))
        return fail("dxf: " + err);
    for (const auto& p : seg.panels)
        sf::writeObj((outDir / ("panel" + std::to_string(p.id) + ".obj")).string(),
                     p.toTriMesh());
    std::cout << "exported pattern.svg, pattern.dxf, panel OBJs -> " << outDir << "\n";

    if (!projectPath.empty()) {
        sf::Project proj;
        proj.sourcePath = sourcePath;
        proj.mesh = mesh;
        proj.seams = std::move(seams);
        proj.panels = seg.panels;
        proj.relations = rels;
        proj.regularized = regularized;
        proj.flattenerName = flattenerName;
        proj.validationText = validationText;
        if (!proj.save(projectPath, &err)) return fail("project save: " + err);
        std::cout << "project saved -> " << projectPath << "\n";
    }
    return 0;
}

int cmdPipeline(const std::map<std::string, std::string>& a) {
    if (!a.count("mesh") || !a.count("seams") || !a.count("out"))
        return fail("--mesh, --seams, --out required");
    sf::TriMesh mesh;
    sf::ValidationReport rep;
    std::string err;
    if (!loadAndValidate(a.at("mesh"), mesh, rep, err)) return fail(err);
    std::cerr << rep.toText();
    if (!rep.flattenable()) return fail("mesh has blocking validation errors");
    auto seams = loadSeams(a.at("seams"), err);
    if (!err.empty()) return fail(err);
    if (seams.empty()) return fail("no seams in " + a.at("seams"));
    json metrics;
    int rc = runPipeline(mesh, std::move(seams),
                         a.count("flattener") ? a.at("flattener") : "arap",
                         a.at("out"), a.count("heatmap") > 0,
                         a.count("project") ? a.at("project") : "",
                         a.at("mesh"), rep.toText(), metrics);
    if (rc == 0) {
        std::ofstream mf(fs::path(a.at("out")) / "metrics.json");
        mf << metrics.dump(1) << "\n";
    }
    return rc;
}

int cmdAuto(const std::map<std::string, std::string>& a) {
    if (!a.count("mesh") || !a.count("truth") || !a.count("out"))
        return fail("--mesh, --truth, --out required");
    std::string baseline = a.count("baseline") ? a.at("baseline") : "silhouette";
    sf::TriMesh mesh;
    sf::ValidationReport rep;
    std::string err;
    if (!loadAndValidate(a.at("mesh"), mesh, rep, err)) return fail(err);

    // predicted face -> panel/chart label, per baseline
    std::vector<int> pred(mesh.F.size(), -1);
    int predPanels = 0;
    json baselineInfo;
    if (baseline == "silhouette") {
        auto prop = sf::proposeSideSeams(mesh);
        std::cerr << prop.log;
        if (prop.seams.size() < 2) return fail("proposal produced fewer than two seams");
        auto seg = sf::segmentBySeams(mesh, prop.seams);
        for (const auto& p : seg.problems) std::cerr << "segmentation: " << p << "\n";
        for (const auto& p : seg.panels)
            for (int f : p.faces) pred[f] = p.id;
        predPanels = (int)seg.panels.size();
        json seamsJson = json::array();
        for (const auto& s : prop.seams)
            seamsJson.push_back({{"confidence", s.confidence}, {"evidence", s.evidence}});
        baselineInfo["proposedSeams"] = seamsJson;
    } else if (baseline == "dcharts") {
        auto dc = sf::dchartsSegment(mesh);
        std::cerr << dc.log;
        if (dc.chartCount < 1) return fail("d-charts produced no charts");
        pred = dc.faceChart;
        predPanels = dc.chartCount;
        baselineInfo["chartRmsFit"] = dc.chartRmsFit;
        baselineInfo["iterations"] = dc.iterationsRun;
        baselineInfo["note"] =
            "chart boundaries minimise developability error, not construction "
            "convention; cut placement on smooth garments is arbitrary";
    } else {
        return fail("unknown baseline '" + baseline + "' (silhouette|dcharts)");
    }

    std::ifstream tf(a.at("truth"));
    if (!tf) return fail("cannot open " + a.at("truth"));
    json jt;
    tf >> jt;
    std::vector<int> gtLabel = jt.value("faceLabel", std::vector<int>{});
    int gtPanels = jt.value("panelCount", 0);
    if ((int)gtLabel.size() != (int)mesh.F.size())
        return fail("ground truth face count mismatch");

    // best-match IoU per ground-truth panel (greedy over predicted panels)
    json report;
    report["baseline"] = baseline;
    report["baselineInfo"] = baselineInfo;
    report["predictedPanelCount"] = predPanels;
    report["truthPanelCount"] = gtPanels;
    double meanIoU = 0;
    json ious = json::array();
    for (int g = 0; g < gtPanels; ++g) {
        double best = 0;
        for (int pp = 0; pp < predPanels; ++pp) {
            int inter = 0, uni = 0;
            for (size_t f = 0; f < pred.size(); ++f) {
                bool inG = gtLabel[f] == g, inP = pred[f] == pp;
                inter += inG && inP;
                uni += inG || inP;
            }
            if (uni > 0) best = std::max(best, (double)inter / uni);
        }
        ious.push_back(best);
        meanIoU += best / gtPanels;
    }
    report["perPanelIoU"] = ious;
    report["meanIoU"] = meanIoU;

    fs::create_directories(a.at("out"));
    std::ofstream rf(fs::path(a.at("out")) / "auto_segmentation_report.json");
    rf << report.dump(1) << "\n";
    std::cout << "baseline " << baseline << ": panel count " << predPanels
              << " (truth " << gtPanels << "), mean IoU " << meanIoU << "\n";
    // dcharts is an analysis baseline: report, do not gate on IoU
    if (baseline == "dcharts") return 0;
    return meanIoU > 0.5 ? 0 : 2;
}

int cmdMatch(const std::map<std::string, std::string>& a) {
    if (!a.count("mesh") || !a.count("out")) return fail("--mesh and --out required");
    sf::TriMesh mesh;
    std::string err;
    auto importer = sf::makeImporterFor(a.at("mesh"));
    auto res = importer->load(a.at("mesh"));
    if (!res.success) return fail(res.message);
    mesh = std::move(res.mesh);
    // Pre-cut panels frequently have exactly coincident boundary vertices
    // (digital cuts); welding would silently sew the panels back together,
    // so duplicate welding is disabled for this workflow (DECISION_LOG D16).
    sf::ValidationOptions vopt;
    vopt.weldDuplicates = false;
    sf::ValidationReport rep = sf::validateAndRepair(mesh, vopt);
    std::cerr << rep.toText();
    if (!rep.flattenable()) return fail("mesh has blocking validation errors");

    // disconnected components become one panel each (no cuts performed)
    auto seg = sf::segmentBySeams(mesh, {});
    for (const auto& p : seg.problems) std::cerr << "segmentation: " << p << "\n";
    if (seg.panels.size() < 2)
        return fail("pre-cut matching needs >= 2 disconnected panel meshes; found " +
                    std::to_string(seg.panels.size()));
    std::cout << seg.panels.size() << " panels (disconnected components)\n";

    auto match = sf::proposeBoundaryMatches(seg.panels);
    std::cerr << match.log;
    if (match.relations.empty()) return fail("no boundary matches found");

    fs::path outDir = a.at("out");
    fs::create_directories(outDir);

    auto flattener = sf::makeFlattener(a.count("flattener") ? a.at("flattener") : "arap");
    if (!flattener) return fail("unknown flattener");
    for (auto& p : seg.panels) {
        auto fr = flattener->flatten(p);
        if (!fr.success)
            return fail("flatten panel " + std::to_string(p.id) + ": " + fr.message);
        p.UV = fr.UV;
    }
    sf::updateRelationLengths(match.relations, seg.panels);

    json metrics;
    metrics["panelCount"] = seg.panels.size();
    json relsJson = json::array();
    for (const auto& r : match.relations) {
        std::cout << "seam " << r.seamId << ": panel " << r.a.panelId << " <-> panel "
                  << r.b.panelId << ", confidence " << r.confidence
                  << ", 2D length mismatch " << r.lengthMismatch2d * 100 << "%"
                  << (r.note.empty() ? "" : (" [" + r.note + "]")) << "\n";
        relsJson.push_back({{"seamId", r.seamId},
                            {"panelA", r.a.panelId},
                            {"panelB", r.b.panelId},
                            {"confidence", r.confidence},
                            {"lengthMismatch2d", r.lengthMismatch2d},
                            {"note", r.note}});
    }
    metrics["relations"] = relsJson;
    metrics["unmatchedArcs"] = match.unmatchedArcs;
    for (const auto& u : match.unmatchedArcs) std::cout << u << "\n";

    double tol = 0.004 * mesh.bbox().diagonal().norm();
    sf::RegularizeOptions ro;
    ro.tolerance = tol;
    sf::CurveFitOptions cfo;
    cfo.tolerance = tol;
    std::vector<std::vector<sf::RegularizedLoop>> regularized;
    for (const auto& p : seg.panels) {
        std::vector<sf::RegularizedLoop> per;
        sf::TriMesh pm = p.toTriMesh();
        for (const auto& loop : pm.boundaryLoops()) {
            std::vector<sf::Vec2> pts;
            for (int v : loop) pts.push_back(p.UV[v]);
            auto reg = sf::regularizeLoop(pts, ro);
            sf::fitLoopCurves(reg, cfo);
            per.push_back(std::move(reg));
        }
        regularized.push_back(std::move(per));
    }

    if (!sf::writeSvg((outDir / "pattern.svg").string(), seg.panels, regularized,
                      match.relations, {}, &err))
        return fail("svg: " + err);
    std::ofstream mf(outDir / "metrics.json");
    mf << metrics.dump(1) << "\n";
    if (a.count("project")) {
        sf::Project proj;
        proj.sourcePath = a.at("mesh");
        proj.mesh = mesh;
        proj.seams = match.seams;
        proj.panels = seg.panels;
        proj.relations = match.relations;
        proj.regularized = regularized;
        proj.flattenerName = a.count("flattener") ? a.at("flattener") : "arap";
        proj.validationText = rep.toText();
        if (!proj.save(a.at("project"), &err)) return fail("project save: " + err);
        std::cout << "project saved -> " << a.at("project") << "\n";
    }
    std::cout << "matched " << match.relations.size() << " seam pair(s), "
              << match.unmatchedArcs.size() << " arc(s) unmatched -> " << outDir << "\n";
    return 0;
}

int cmdRoundtrip(const std::map<std::string, std::string>& a) {
    if (!a.count("project")) return fail("--project required");
    sf::Project p;
    std::string err;
    if (!sf::Project::load(a.at("project"), p, &err)) return fail(err);
    std::string tmp = a.at("project") + ".resaved";
    if (!p.save(tmp, &err)) return fail(err);
    sf::Project p2;
    if (!sf::Project::load(tmp, p2, &err)) return fail(err);
    bool same = p.toJson() == p2.toJson();
    std::cout << "project: " << p.panels.size() << " panels, " << p.seams.size()
              << " seams, " << p.relations.size() << " relations; round-trip "
              << (same ? "IDENTICAL" : "DIFFERS") << "\n";
    fs::remove(tmp);
    return same ? 0 : 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: seamforge-cli <validate|propose|pipeline|auto|match|roundtrip> [options]\n";
        return 1;
    }
    std::string cmd = argv[1];
    auto args = parseArgs(argc, argv, 2);
    if (cmd == "validate") return cmdValidate(args);
    if (cmd == "propose") return cmdPropose(args);
    if (cmd == "pipeline") return cmdPipeline(args);
    if (cmd == "auto") return cmdAuto(args);
    if (cmd == "match") return cmdMatch(args);
    if (cmd == "roundtrip") return cmdRoundtrip(args);
    return fail("unknown command " + cmd);
}
