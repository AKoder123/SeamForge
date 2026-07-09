#pragma once
// Mesh import/export behind a replaceable interface.
// The default importer wraps Assimp (OBJ, glTF/GLB, PLY, STL, ...).

#include "Mesh.h"
#include <memory>
#include <string>

namespace sf {

struct ImportResult {
    bool success = false;
    std::string message;
    int submeshCount = 0;   // scene meshes merged into one TriMesh
    TriMesh mesh;
};

class IMeshImporter {
public:
    virtual ~IMeshImporter() = default;
    virtual ImportResult load(const std::string& path) = 0;
    virtual std::string name() const = 0;
};

std::unique_ptr<IMeshImporter> makeAssimpImporter();

// Native OBJ parser that preserves vertex order and indexing exactly as in
// the file (Assimp expands OBJ vertices per-face, which breaks external
// vertex references such as seam files). Triangulates convex polygons by
// fanning.
std::unique_ptr<IMeshImporter> makeObjImporter();

// Dispatch: .obj -> native parser (index-preserving), everything else Assimp.
std::unique_ptr<IMeshImporter> makeImporterFor(const std::string& path);

// Minimal OBJ writer (positions + faces), used for panel export & debugging.
bool writeObj(const std::string& path, const TriMesh& m, std::string* err = nullptr);

} // namespace sf
