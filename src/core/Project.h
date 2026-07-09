#pragma once
// Structured JSON project format ("SeamForge Reverse Project", .sfrproj).
// Fully self-contained: embeds the source mesh, seams, panels with 3D->2D
// correspondence, seam relations and regularisation state, so a saved
// reconstruction can be reopened and edited without external files.
//
// The schema is versioned; loaders must reject newer major versions.

#include "Flatten.h"
#include "Regularize.h"
#include "Relations.h"
#include "Seam.h"
#include "Segmentation.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace sf {

struct Project {
    static constexpr int kSchemaVersion = 1;

    std::string sourcePath;                 // informational only
    std::string units = "m";
    TriMesh mesh;                           // validated source mesh
    std::vector<Seam> seams;
    std::vector<Panel> panels;              // with UV when flattened
    std::vector<SeamRelation> relations;
    std::vector<std::vector<RegularizedLoop>> regularized;   // per panel
    std::string flattenerName;              // "lscm" / "arap" / ""
    std::string validationText;             // last validation report

    nlohmann::json toJson() const;
    static Project fromJson(const nlohmann::json& j, std::string* err = nullptr);

    bool save(const std::string& path, std::string* err = nullptr) const;
    static bool load(const std::string& path, Project& out, std::string* err = nullptr);
};

} // namespace sf
