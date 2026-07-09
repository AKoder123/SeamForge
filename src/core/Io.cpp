#include "Io.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <cstdio>
#include <fstream>

namespace sf {

namespace {

class AssimpImporter final : public IMeshImporter {
public:
    ImportResult load(const std::string& path) override {
        ImportResult r;
        Assimp::Importer imp;
        // Triangulate only. We deliberately do NOT join identical vertices or
        // remove degenerates here: the validation stage does that with
        // explicit reporting.
        const aiScene* scene =
            imp.ReadFile(path, aiProcess_Triangulate | aiProcess_SortByPType);
        if (!scene || !scene->mRootNode) {
            r.message = imp.GetErrorString();
            return r;
        }
        mergeNode(scene, scene->mRootNode, aiMatrix4x4(), r);
        r.submeshCount = countMeshes(scene->mRootNode);
        if (r.mesh.F.empty()) {
            r.message = "no triangle faces found in " + path;
            return r;
        }
        r.success = true;
        return r;
    }
    std::string name() const override { return "assimp"; }

private:
    static int countMeshes(const aiNode* n) {
        int c = (int)n->mNumMeshes;
        for (unsigned i = 0; i < n->mNumChildren; ++i) c += countMeshes(n->mChildren[i]);
        return c;
    }
    static void mergeNode(const aiScene* scene, const aiNode* node,
                          const aiMatrix4x4& parent, ImportResult& r) {
        aiMatrix4x4 xf = parent * node->mTransformation;
        for (unsigned mi = 0; mi < node->mNumMeshes; ++mi) {
            const aiMesh* mesh = scene->mMeshes[node->mMeshes[mi]];
            if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) continue;
            int base = (int)r.mesh.V.size();
            for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
                aiVector3D p = xf * mesh->mVertices[v];
                r.mesh.V.emplace_back(p.x, p.y, p.z);
            }
            for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
                const aiFace& face = mesh->mFaces[f];
                if (face.mNumIndices != 3) continue;
                r.mesh.F.push_back({base + (int)face.mIndices[0],
                                    base + (int)face.mIndices[1],
                                    base + (int)face.mIndices[2]});
            }
        }
        for (unsigned c = 0; c < node->mNumChildren; ++c)
            mergeNode(scene, node->mChildren[c], xf, r);
    }
};

class ObjImporter final : public IMeshImporter {
public:
    ImportResult load(const std::string& path) override {
        ImportResult r;
        std::ifstream f(path);
        if (!f) {
            r.message = "cannot open " + path;
            return r;
        }
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("v ", 0) == 0) {
                double x, y, z;
                if (std::sscanf(line.c_str(), "v %lf %lf %lf", &x, &y, &z) == 3)
                    r.mesh.V.emplace_back(x, y, z);
            } else if (line.rfind("f ", 0) == 0) {
                std::vector<int> idx;
                const char* s = line.c_str() + 2;
                while (*s) {
                    while (*s == ' ') ++s;
                    if (!*s) break;
                    long v = std::strtol(s, const_cast<char**>(&s), 10);
                    if (v < 0) v = (long)r.mesh.V.size() + 1 + v;   // negative refs
                    idx.push_back((int)v - 1);
                    while (*s && *s != ' ') ++s;   // skip /vt/vn part
                }
                for (size_t k = 2; k < idx.size(); ++k)             // fan
                    r.mesh.F.push_back({idx[0], idx[k - 1], idx[k]});
            }
        }
        if (r.mesh.F.empty()) {
            r.message = "no faces found in " + path;
            return r;
        }
        for (const auto& t : r.mesh.F)
            for (int k = 0; k < 3; ++k)
                if (t[k] < 0 || t[k] >= (int)r.mesh.V.size()) {
                    r.message = "face index out of range in " + path;
                    return r;
                }
        r.submeshCount = 1;
        r.success = true;
        return r;
    }
    std::string name() const override { return "obj-native"; }
};

} // namespace

std::unique_ptr<IMeshImporter> makeAssimpImporter() {
    return std::make_unique<AssimpImporter>();
}

std::unique_ptr<IMeshImporter> makeObjImporter() {
    return std::make_unique<ObjImporter>();
}

std::unique_ptr<IMeshImporter> makeImporterFor(const std::string& path) {
    auto dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == ".obj") return makeObjImporter();
    return makeAssimpImporter();
}

bool writeObj(const std::string& path, const TriMesh& m, std::string* err) {
    std::ofstream os(path);
    if (!os) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    os << "# SeamForge Reverse OBJ export\n";
    char buf[128];
    for (const auto& p : m.V) {
        std::snprintf(buf, sizeof buf, "v %.9g %.9g %.9g\n", p.x(), p.y(), p.z());
        os << buf;
    }
    for (const auto& f : m.F)
        os << "f " << f[0] + 1 << ' ' << f[1] + 1 << ' ' << f[2] + 1 << '\n';
    return bool(os);
}

} // namespace sf
