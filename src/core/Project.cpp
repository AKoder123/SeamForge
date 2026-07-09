#include "Project.h"

#include <fstream>

namespace sf {

using nlohmann::json;

namespace {

json vec3ArrayToJson(const std::vector<Vec3>& v) {
    json a = json::array();
    for (const auto& p : v) a.push_back({p.x(), p.y(), p.z()});
    return a;
}
std::vector<Vec3> vec3ArrayFromJson(const json& a) {
    std::vector<Vec3> v;
    v.reserve(a.size());
    for (const auto& e : a) v.emplace_back(e[0].get<double>(), e[1].get<double>(), e[2].get<double>());
    return v;
}
json vec2ArrayToJson(const std::vector<Vec2>& v) {
    json a = json::array();
    for (const auto& p : v) a.push_back({p.x(), p.y()});
    return a;
}
std::vector<Vec2> vec2ArrayFromJson(const json& a) {
    std::vector<Vec2> v;
    v.reserve(a.size());
    for (const auto& e : a) v.emplace_back(e[0].get<double>(), e[1].get<double>());
    return v;
}
json facesToJson(const std::vector<std::array<int, 3>>& F) {
    json a = json::array();
    for (const auto& f : F) a.push_back({f[0], f[1], f[2]});
    return a;
}
std::vector<std::array<int, 3>> facesFromJson(const json& a) {
    std::vector<std::array<int, 3>> F;
    F.reserve(a.size());
    for (const auto& e : a) F.push_back({e[0].get<int>(), e[1].get<int>(), e[2].get<int>()});
    return F;
}

const char* seamSourceName(Seam::Source s) {
    switch (s) {
        case Seam::Source::Manual: return "manual";
        case Seam::Source::Proposed: return "proposed";
        case Seam::Source::MeshBoundary: return "mesh-boundary";
    }
    return "manual";
}
Seam::Source seamSourceFromName(const std::string& n) {
    if (n == "proposed") return Seam::Source::Proposed;
    if (n == "mesh-boundary") return Seam::Source::MeshBoundary;
    return Seam::Source::Manual;
}

} // namespace

json Project::toJson() const {
    json j;
    j["format"] = "seamforge-reverse-project";
    j["schemaVersion"] = kSchemaVersion;
    j["units"] = units;
    j["sourcePath"] = sourcePath;
    j["flattener"] = flattenerName;
    j["validationText"] = validationText;

    j["mesh"] = {{"vertices", vec3ArrayToJson(mesh.V)}, {"faces", facesToJson(mesh.F)}};

    j["seams"] = json::array();
    for (const auto& s : seams)
        j["seams"].push_back({{"id", s.id},
                              {"vertices", s.vertices},
                              {"source", seamSourceName(s.source)},
                              {"confidence", s.confidence},
                              {"evidence", s.evidence}});

    j["panels"] = json::array();
    for (const auto& p : panels) {
        json jp = {{"id", p.id},
                   {"label", p.label},
                   {"vertices", vec3ArrayToJson(p.V)},
                   {"faces", facesToJson(p.F)},
                   {"toOrigVertex", p.toOrigV},
                   {"origFaces", p.origFace},
                   {"uv", vec2ArrayToJson(p.UV)}};
        jp["segments"] = json::array();
        for (const auto& s : p.segments)
            jp["segments"].push_back({{"seamId", s.seamId}, {"localVerts", s.localVerts}});
        jp["boundaryLoops"] = p.boundaryLoopsLocal;
        j["panels"].push_back(std::move(jp));
    }

    j["relations"] = json::array();
    for (const auto& r : relations)
        j["relations"].push_back({{"seamId", r.seamId},
                                  {"panelA", r.a.panelId},
                                  {"vertsA", r.a.localVerts},
                                  {"panelB", r.b.panelId},
                                  {"vertsB", r.b.localVerts},
                                  {"reversed", r.reversed},
                                  {"confidence", r.confidence},
                                  {"source", r.source},
                                  {"length3d", r.length3d},
                                  {"lengthMismatch2d", r.lengthMismatch2d},
                                  {"note", r.note}});

    j["regularized"] = json::array();
    for (const auto& perPanel : regularized) {
        json jp = json::array();
        for (const auto& reg : perPanel)
            jp.push_back({{"raw", vec2ArrayToJson(reg.raw)},
                          {"keptIdx", reg.keptIdx},
                          {"isCorner", reg.isCorner},
                          {"isStraight", reg.isStraight},
                          {"maxDeviation", reg.maxDeviation}});
        j["regularized"].push_back(std::move(jp));
    }
    return j;
}

Project Project::fromJson(const json& j, std::string* err) {
    Project p;
    if (j.value("format", "") != "seamforge-reverse-project") {
        if (err) *err = "not a SeamForge Reverse project file";
        return p;
    }
    int v = j.value("schemaVersion", 0);
    if (v > kSchemaVersion) {
        if (err) *err = "project schema version " + std::to_string(v) + " is newer than supported";
        return p;
    }
    p.units = j.value("units", "m");
    p.sourcePath = j.value("sourcePath", "");
    p.flattenerName = j.value("flattener", "");
    p.validationText = j.value("validationText", "");

    p.mesh.V = vec3ArrayFromJson(j["mesh"]["vertices"]);
    p.mesh.F = facesFromJson(j["mesh"]["faces"]);
    p.mesh.buildAdjacency();

    for (const auto& js : j.value("seams", json::array())) {
        Seam s;
        s.id = js.value("id", -1);
        s.vertices = js.value("vertices", std::vector<int>{});
        s.source = seamSourceFromName(js.value("source", "manual"));
        s.confidence = js.value("confidence", 1.0);
        s.evidence = js.value("evidence", "");
        p.seams.push_back(std::move(s));
    }

    for (const auto& jp : j.value("panels", json::array())) {
        Panel pa;
        pa.id = jp.value("id", -1);
        pa.label = jp.value("label", "unknown");
        pa.V = vec3ArrayFromJson(jp["vertices"]);
        pa.F = facesFromJson(jp["faces"]);
        pa.toOrigV = jp.value("toOrigVertex", std::vector<int>{});
        pa.origFace = jp.value("origFaces", std::vector<int>{});
        pa.faces = pa.origFace;
        pa.UV = vec2ArrayFromJson(jp.value("uv", json::array()));
        for (const auto& js : jp.value("segments", json::array())) {
            BoundarySegment seg;
            seg.seamId = js.value("seamId", -1);
            seg.localVerts = js.value("localVerts", std::vector<int>{});
            pa.segments.push_back(std::move(seg));
        }
        pa.boundaryLoopsLocal =
            jp.value("boundaryLoops", std::vector<std::vector<int>>{});
        p.panels.push_back(std::move(pa));
    }

    for (const auto& jr : j.value("relations", json::array())) {
        SeamRelation r;
        r.seamId = jr.value("seamId", -1);
        r.a.panelId = jr.value("panelA", -1);
        r.a.localVerts = jr.value("vertsA", std::vector<int>{});
        r.b.panelId = jr.value("panelB", -1);
        r.b.localVerts = jr.value("vertsB", std::vector<int>{});
        r.reversed = jr.value("reversed", false);
        r.confidence = jr.value("confidence", 1.0);
        r.source = jr.value("source", "cut-ancestry");
        r.length3d = jr.value("length3d", 0.0);
        r.lengthMismatch2d = jr.value("lengthMismatch2d", 0.0);
        r.note = jr.value("note", "");
        p.relations.push_back(std::move(r));
    }

    for (const auto& jpp : j.value("regularized", json::array())) {
        std::vector<RegularizedLoop> perPanel;
        for (const auto& jr : jpp) {
            RegularizedLoop reg;
            reg.raw = vec2ArrayFromJson(jr["raw"]);
            reg.keptIdx = jr.value("keptIdx", std::vector<int>{});
            for (int i : reg.keptIdx) reg.simplified.push_back(reg.raw[i]);
            reg.isCorner = jr.value("isCorner", std::vector<bool>{});
            reg.isStraight = jr.value("isStraight", std::vector<bool>{});
            reg.maxDeviation = jr.value("maxDeviation", 0.0);
            perPanel.push_back(std::move(reg));
        }
        p.regularized.push_back(std::move(perPanel));
    }
    return p;
}

bool Project::save(const std::string& path, std::string* err) const {
    std::ofstream f(path);
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    f << toJson().dump(1) << "\n";
    return bool(f);
}

bool Project::load(const std::string& path, Project& out, std::string* err) {
    std::ifstream f(path);
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
    std::string perr;
    out = fromJson(j, &perr);
    if (!perr.empty()) {
        if (err) *err = perr;
        return false;
    }
    return true;
}

} // namespace sf
