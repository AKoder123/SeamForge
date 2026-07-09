// Generates the benchmark/evaluation meshes with ground truth under data/.
// Usage: make-test-meshes <output-dir>

#include "core/Io.h"
#include "core/Procedural.h"
#include "core/Segmentation.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

void writeMeshCase(const fs::path& dir, const std::string& name,
                   const sf::TriMesh& m, const sf::GroundTruth& gt) {
    std::string err;
    if (!sf::writeObj((dir / (name + ".obj")).string(), m, &err)) {
        std::cerr << "write " << name << ": " << err << "\n";
        std::exit(1);
    }
    json j;
    j["faceLabel"] = gt.faceLabel;
    j["panelCount"] = gt.panelCount;
    j["seamPaths"] = gt.seamPaths;
    if (!gt.dartPaths.empty()) j["dartPaths"] = gt.dartPaths;
    std::ofstream gf(dir / (name + ".gt.json"));
    gf << j.dump() << "\n";
    // seams file usable directly by `seamforge-cli pipeline`
    json js;
    js["seams"] = json::array();
    for (size_t i = 0; i < gt.seamPaths.size(); ++i)
        js["seams"].push_back({{"id", (int)i}, {"vertices", gt.seamPaths[i]}});
    std::ofstream sfj(dir / (name + ".seams.json"));
    sfj << js.dump() << "\n";
    std::cout << name << ": " << m.V.size() << " verts, " << m.F.size() << " faces\n";
}

void writeCase(const fs::path& dir, const std::string& name,
               const sf::SkirtOptions& opt) {
    sf::GroundTruth gt;
    sf::TriMesh m = sf::makeSkirt(opt, &gt);
    writeMeshCase(dir, name, m, gt);
}

} // namespace

int main(int argc, char** argv) {
    fs::path dir = argc > 1 ? argv[1] : "data/meshes";
    fs::create_directories(dir);

    sf::SkirtOptions simple;                       // developable frustum, 2 panels
    writeCase(dir, "skirt_simple", simple);

    sf::SkirtOptions aline = simple;               // curved profile: not developable
    aline.bulge = 0.06;
    writeCase(dir, "skirt_aline", aline);

    sf::SkirtOptions four = simple;                // 4-panel skirt
    four.panels = 4;
    writeCase(dir, "skirt_fourpanel", four);

    sf::SkirtOptions noisy = simple;               // jittered vertices (scan-like)
    noisy.noise = 0.002;
    writeCase(dir, "skirt_noisy", noisy);

    sf::SkirtOptions dense = simple;               // higher resolution
    dense.radialSegments = 128;
    dense.rings = 40;
    writeCase(dir, "skirt_dense", dense);

    sf::SkirtOptions hard = simple;                // deliberately difficult:
    hard.bulge = 0.12;                             // strong curvature + noise
    hard.noise = 0.004;
    writeCase(dir, "skirt_hard", hard);

    sf::SkirtOptions darted = simple;              // waist darts (crease evidence;
    darted.darts = 2;                              // dart paths in gt dartPaths)
    {
        sf::GroundTruth gt;
        sf::TriMesh m = sf::makeSkirt(darted, &gt);
        writeMeshCase(dir, "skirt_darts", m, gt);
    }

    {
        sf::GroundTruth gt;                        // kimono/boxy T-shirt, 2 panels,
        sf::TriMesh m = sf::makeBoxyTee({}, &gt);  // 4 openings, 4 seams
        writeMeshCase(dir, "tshirt_boxy", m, gt);
    }
    {
        sf::GroundTruth gt;                        // pyjama trousers, 2 panels,
        sf::TriMesh m = sf::makeFlatTrousers({}, &gt);  // 3 openings, 3 seams
        writeMeshCase(dir, "trousers_flat", m, gt);
    }

    // pre-cut variant: the simple skirt's two panels as disconnected
    // components in one OBJ (exercises boundary matching, `seamforge-cli match`)
    {
        sf::GroundTruth gt;
        sf::TriMesh m = sf::makeSkirt(sf::SkirtOptions{}, &gt);
        std::vector<sf::Seam> seams;
        for (size_t i = 0; i < gt.seamPaths.size(); ++i) {
            sf::Seam s;
            s.id = (int)i;
            s.vertices = gt.seamPaths[i];
            seams.push_back(std::move(s));
        }
        auto seg = sf::segmentBySeams(m, seams);
        sf::TriMesh merged;
        for (const auto& p : seg.panels) {
            int base = (int)merged.V.size();
            merged.V.insert(merged.V.end(), p.V.begin(), p.V.end());
            for (const auto& f : p.F)
                merged.F.push_back({f[0] + base, f[1] + base, f[2] + base});
        }
        std::string err;
        if (!sf::writeObj((dir / "skirt_precut.obj").string(), merged, &err)) {
            std::cerr << "write skirt_precut: " << err << "\n";
            return 1;
        }
        std::cout << "skirt_precut: " << merged.V.size() << " verts, "
                  << merged.F.size() << " faces (2 components)\n";
    }

    std::cout << "done -> " << dir << "\n";
    return 0;
}
