// Generates the benchmark/evaluation meshes with ground truth under data/.
// Usage: make-test-meshes <output-dir>

#include "core/Io.h"
#include "core/Procedural.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

void writeCase(const fs::path& dir, const std::string& name,
               const sf::SkirtOptions& opt) {
    sf::GroundTruth gt;
    sf::TriMesh m = sf::makeSkirt(opt, &gt);
    std::string err;
    if (!sf::writeObj((dir / (name + ".obj")).string(), m, &err)) {
        std::cerr << "write " << name << ": " << err << "\n";
        std::exit(1);
    }
    json j;
    j["faceLabel"] = gt.faceLabel;
    j["panelCount"] = gt.panelCount;
    j["seamPaths"] = gt.seamPaths;
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

    std::cout << "done -> " << dir << "\n";
    return 0;
}
