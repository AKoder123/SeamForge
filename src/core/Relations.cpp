#include "Relations.h"

#include <algorithm>
#include <unordered_map>

namespace sf {

double SeamSide::length2d(const Panel& p) const {
    if (p.UV.empty()) return -1;
    double s = 0;
    for (size_t i = 0; i + 1 < localVerts.size(); ++i)
        s += (p.UV[localVerts[i]] - p.UV[localVerts[i + 1]]).norm();
    return s;
}

std::vector<SeamRelation> deriveSeamRelations(const TriMesh& mesh,
                                              const std::vector<Seam>& seams,
                                              const SegmentationResult& seg) {
    std::vector<SeamRelation> out;

    // orig vertex -> local vertex per panel
    std::vector<std::unordered_map<int, int>> o2l(seg.panels.size());
    for (size_t pi = 0; pi < seg.panels.size(); ++pi)
        for (int lv = 0; lv < (int)seg.panels[pi].toOrigV.size(); ++lv)
            o2l[pi][seg.panels[pi].toOrigV[lv]] = lv;

    // face -> panel
    std::unordered_map<int, int> facePanel;
    for (const auto& p : seg.panels)
        for (int f : p.faces) facePanel[f] = p.id;

    for (const auto& s : seams) {
        if (s.source == Seam::Source::MeshBoundary) continue;
        SeamRelation rel;
        rel.seamId = s.id;
        rel.length3d = s.length3d(mesh);

        // panels adjacent to the seam edges
        std::vector<int> adj;
        for (int e : seamEdgeIndices(mesh, s))
            for (int f : mesh.edges[e].faces) {
                int pid = facePanel.count(f) ? facePanel[f] : -1;
                if (pid >= 0 && std::find(adj.begin(), adj.end(), pid) == adj.end())
                    adj.push_back(pid);
            }
        std::sort(adj.begin(), adj.end());

        if (adj.size() != 2) {
            rel.confidence = 0.3;
            rel.note = adj.size() < 2
                ? "seam does not separate two panels (non-separating cut or same panel on both sides); pairing ambiguous"
                : "seam borders more than two panels; pairing ambiguous";
            if (adj.empty()) { out.push_back(rel); continue; }
        }

        auto buildSide = [&](int pid) {
            SeamSide side;
            side.panelId = pid;
            bool complete = true;
            for (int ov : s.vertices) {
                auto it = o2l[pid].find(ov);
                if (it == o2l[pid].end()) { complete = false; continue; }
                side.localVerts.push_back(it->second);
            }
            if (!complete) {
                rel.note += (rel.note.empty() ? "" : "; ");
                rel.note += "partial match on panel " + std::to_string(pid);
                rel.confidence = std::min(rel.confidence, 0.6);
            }
            return side;
        };
        rel.a = buildSide(adj[0]);
        if (adj.size() >= 2) rel.b = buildSide(adj[1]);
        out.push_back(std::move(rel));
    }
    return out;
}

void updateRelationLengths(std::vector<SeamRelation>& rels,
                           const std::vector<Panel>& panels) {
    auto panelById = [&](int id) -> const Panel* {
        for (const auto& p : panels)
            if (p.id == id) return &p;
        return nullptr;
    };
    for (auto& r : rels) {
        const Panel* pa = panelById(r.a.panelId);
        const Panel* pb = panelById(r.b.panelId);
        if (!pa || !pb || pa->UV.empty() || pb->UV.empty()) continue;
        double la = r.a.length2d(*pa), lb = r.b.length2d(*pb);
        if (la > 0 && lb > 0)
            r.lengthMismatch2d = std::abs(la - lb) / std::max(la, lb);
    }
}

} // namespace sf
