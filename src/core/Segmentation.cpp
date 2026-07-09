#include "Segmentation.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace sf {

TriMesh Panel::toTriMesh() const {
    TriMesh m;
    m.V = V;
    m.F = F;
    m.buildAdjacency();
    return m;
}

double Panel::area3d() const {
    double a = 0;
    for (const auto& t : F)
        a += 0.5 * (V[t[1]] - V[t[0]]).cross(V[t[2]] - V[t[0]]).norm();
    return a;
}

SegmentationResult segmentBySeams(const TriMesh& m, const std::vector<Seam>& seams) {
    SegmentationResult out;
    if (!m.adjacencyValid()) {
        out.problems.push_back("mesh adjacency not built");
        return out;
    }

    // 1. collect seam edges; map edge -> seam id
    std::vector<char> blocked(m.edges.size(), 0);
    std::unordered_map<int, int> edgeSeam;
    for (const auto& s : seams) {
        std::string err = validateSeamPath(m, s);
        if (!err.empty()) {
            out.problems.push_back("seam " + std::to_string(s.id) + ": " + err);
            return out;
        }
        for (int e : seamEdgeIndices(m, s)) {
            blocked[e] = 1;
            edgeSeam[e] = s.id;
        }
    }

    // 2. seam endpoints must be anchored (mesh boundary or another seam),
    //    otherwise the cut cannot separate anything there.
    for (const auto& s : seams) {
        if (s.closed()) continue;
        for (int endpoint : {s.vertices.front(), s.vertices.back()}) {
            bool anchored = false;
            for (int e : m.vertexEdges[endpoint]) {
                if (m.edges[e].boundary()) anchored = true;
                if (blocked[e] && edgeSeam.count(e) && edgeSeam[e] != s.id) anchored = true;
            }
            if (!anchored)
                out.problems.push_back(
                    "seam " + std::to_string(s.id) + " endpoint vertex " +
                    std::to_string(endpoint) +
                    " is not anchored to the mesh boundary or another seam; the cut will not separate there");
        }
    }

    // 3. face flood fill (seam edges act as walls)
    int nComp = 0;
    std::vector<int> comp = m.faceComponents(nComp, &blocked);
    if (nComp < 2 && !seams.empty())
        out.problems.push_back("seams do not separate the mesh (still one component)");

    // 4. build one panel per component; vertices on seams are duplicated
    //    naturally because each panel owns a private vertex copy.
    out.panels.resize(nComp);
    for (int c = 0; c < nComp; ++c) out.panels[c].id = c;

    std::vector<std::unordered_map<int, int>> o2l(nComp);   // orig vertex -> local per panel
    for (int f = 0; f < (int)m.F.size(); ++f) {
        Panel& p = out.panels[comp[f]];
        auto& map = o2l[comp[f]];
        std::array<int, 3> lf{};
        for (int k = 0; k < 3; ++k) {
            int ov = m.F[f][k];
            auto it = map.find(ov);
            if (it == map.end()) {
                it = map.emplace(ov, (int)p.V.size()).first;
                p.V.push_back(m.V[ov]);
                p.toOrigV.push_back(ov);
            }
            lf[k] = it->second;
        }
        p.F.push_back(lf);
        p.origFace.push_back(f);
        p.faces.push_back(f);
    }

    // 5. per-panel boundary loops + ancestry segments
    for (int c = 0; c < nComp; ++c) {
        Panel& p = out.panels[c];
        TriMesh pm = p.toTriMesh();
        p.boundaryLoopsLocal = pm.boundaryLoops();

        for (const auto& loop : p.boundaryLoopsLocal) {
            const int n = (int)loop.size();
            // ancestry of each boundary edge (loop[i], loop[i+1])
            std::vector<int> anc(n, -1);
            for (int i = 0; i < n; ++i) {
                int a = p.toOrigV[loop[i]], b = p.toOrigV[loop[(i + 1) % n]];
                int oe = m.edgeBetween(a, b);
                anc[i] = (oe >= 0 && edgeSeam.count(oe)) ? edgeSeam[oe] : -1;
            }
            // split loop into maximal runs of equal ancestry
            int start = 0;
            bool uniform = true;
            for (int i = 1; i < n; ++i)
                if (anc[i] != anc[0]) { start = i; uniform = false; break; }
            if (uniform) {
                BoundarySegment seg;
                seg.seamId = anc[0];
                seg.localVerts = loop;
                seg.localVerts.push_back(loop[0]);   // closed run
                p.segments.push_back(std::move(seg));
                continue;
            }
            // rotate so a run boundary is at index `start`
            int i = start;
            int consumed = 0;
            while (consumed < n) {
                BoundarySegment seg;
                seg.seamId = anc[i];
                seg.localVerts.push_back(loop[i]);
                while (consumed < n && anc[i] == seg.seamId) {
                    seg.localVerts.push_back(loop[(i + 1) % n]);
                    i = (i + 1) % n;
                    ++consumed;
                }
                p.segments.push_back(std::move(seg));
            }
        }

        // validate panel is a single component (should be, by construction)
        int pc = 0;
        pm.faceComponents(pc);
        if (pc != 1)
            out.problems.push_back("panel " + std::to_string(c) +
                                   " is not a single connected component");
        if (pm.numNonManifoldEdges() > 0)
            out.problems.push_back("panel " + std::to_string(c) +
                                   " contains non-manifold edges; flattening is unreliable");
        if (p.boundaryLoopsLocal.empty())
            out.problems.push_back("panel " + std::to_string(c) +
                                   " is closed (no boundary); it cannot be flattened without a cut");
    }

    // deterministic panel order: by centroid, then size (already deterministic
    // via face order, but sort defensively by first original face id)
    std::sort(out.panels.begin(), out.panels.end(),
              [](const Panel& a, const Panel& b) { return a.faces.front() < b.faces.front(); });
    for (int i = 0; i < (int)out.panels.size(); ++i) out.panels[i].id = i;

    return out;
}

} // namespace sf
