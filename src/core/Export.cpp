#include "Export.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace sf {

namespace {

std::string fmt(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}

// simple angle-distortion colour ramp: 1.0 -> pale, >=1.5 -> red
std::string heatColor(double angleDist) {
    double t = std::clamp((angleDist - 1.0) / 0.5, 0.0, 1.0);
    int r = (int)(240 + t * 15);
    int g = (int)(240 - t * 170);
    int b = (int)(240 - t * 190);
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", std::clamp(r, 0, 255),
                  std::clamp(g, 0, 255), std::clamp(b, 0, 255));
    return buf;
}

struct Placed {
    Vec2 offset;      // mm
    Vec2 sizeMm;
};

} // namespace

std::string renderSvg(const std::vector<Panel>& panels,
                      const std::vector<std::vector<RegularizedLoop>>& regularized,
                      const std::vector<SeamRelation>& relations,
                      const ExportOptions& opt) {
    // layout: panels left to right
    std::vector<Placed> place(panels.size());
    double x = opt.marginMm, maxH = 0;
    for (size_t i = 0; i < panels.size(); ++i) {
        Vec2 mn(1e300, 1e300), mx(-1e300, -1e300);
        for (const auto& q : panels[i].UV) {
            mn = mn.cwiseMin(q);
            mx = mx.cwiseMax(q);
        }
        Vec2 size = (mx - mn) * opt.unitsToMm;
        place[i].offset = Vec2(x - mn.x() * opt.unitsToMm,
                               opt.marginMm - mn.y() * opt.unitsToMm);
        place[i].sizeMm = size;
        x += size.x() + opt.spacingMm;
        maxH = std::max(maxH, size.y());
    }
    double W = x - opt.spacingMm + opt.marginMm;
    double H = maxH + 2 * opt.marginMm + (opt.drawDimensions ? 14.0 : 0.0);

    auto mm = [&](size_t pi, const Vec2& uv) {
        return Vec2(place[pi].offset.x() + uv.x() * opt.unitsToMm,
                    place[pi].offset.y() + uv.y() * opt.unitsToMm);
    };

    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << fmt(W)
       << "mm\" height=\"" << fmt(H) << "mm\" viewBox=\"0 0 " << fmt(W) << ' '
       << fmt(H) << "\">\n"
       << "<!-- SeamForge Reverse pattern export; units: mm -->\n";

    for (size_t pi = 0; pi < panels.size(); ++pi) {
        const Panel& p = panels[pi];
        os << "<g id=\"panel-" << p.id << "\" data-label=\"" << p.label << "\">\n";

        if (opt.drawHeatmap && !p.UV.empty()) {
            auto fd = computeDistortion(p, p.UV);
            for (size_t f = 0; f < p.F.size(); ++f) {
                const auto& t = p.F[f];
                Vec2 a = mm(pi, p.UV[t[0]]), b = mm(pi, p.UV[t[1]]), c = mm(pi, p.UV[t[2]]);
                os << "<polygon points=\"" << fmt(a.x()) << ',' << fmt(a.y()) << ' '
                   << fmt(b.x()) << ',' << fmt(b.y()) << ' ' << fmt(c.x()) << ','
                   << fmt(c.y()) << "\" fill=\"" << heatColor(fd[f].angleDistortion())
                   << "\" stroke=\"none\"/>\n";
            }
        }

        // raw boundary loops
        TriMesh pm = p.toTriMesh();
        auto loops = pm.boundaryLoops();
        if (opt.drawRawBoundary) {
            for (const auto& loop : loops) {
                os << "<polygon points=\"";
                for (int v : loop) {
                    Vec2 q = mm(pi, p.UV[v]);
                    os << fmt(q.x()) << ',' << fmt(q.y()) << ' ';
                }
                os << "\" fill=\"none\" stroke=\"#aaaaaa\" stroke-width=\"0.3\"/>\n";
            }
        }
        // regularised outline: fitted Bezier path when available, else polyline
        if (pi < regularized.size()) {
            for (const auto& reg : regularized[pi]) {
                if (reg.simplified.empty()) continue;
                if (reg.hasCurves()) {
                    Vec2 s0 = mm(pi, reg.curves.front().p0);
                    os << "<path d=\"M " << fmt(s0.x()) << ' ' << fmt(s0.y());
                    for (const auto& b : reg.curves) {
                        if (b.isLine) {
                            Vec2 q = mm(pi, b.p1);
                            os << " L " << fmt(q.x()) << ' ' << fmt(q.y());
                        } else {
                            Vec2 c0 = mm(pi, b.c0), c1 = mm(pi, b.c1), q = mm(pi, b.p1);
                            os << " C " << fmt(c0.x()) << ' ' << fmt(c0.y()) << ' '
                               << fmt(c1.x()) << ' ' << fmt(c1.y()) << ' '
                               << fmt(q.x()) << ' ' << fmt(q.y());
                        }
                    }
                    os << " Z\" fill=\"none\" stroke=\"#000000\" stroke-width=\"0.6\"/>\n";
                } else {
                    os << "<polygon points=\"";
                    for (const auto& q0 : reg.simplified) {
                        Vec2 q = mm(pi, q0);
                        os << fmt(q.x()) << ',' << fmt(q.y()) << ' ';
                    }
                    os << "\" fill=\"none\" stroke=\"#000000\" stroke-width=\"0.6\"/>\n";
                }
                // corner markers
                for (size_t i = 0; i < reg.simplified.size(); ++i) {
                    if (!reg.isCorner[i]) continue;
                    Vec2 q = mm(pi, reg.simplified[i]);
                    os << "<circle cx=\"" << fmt(q.x()) << "\" cy=\"" << fmt(q.y())
                       << "\" r=\"1.2\" fill=\"#000000\"/>\n";
                }
            }
        }

        // seam labels at segment midpoints
        if (opt.drawSeamLabels) {
            for (const auto& seg : p.segments) {
                if (seg.seamId < 0 || seg.localVerts.empty()) continue;
                Vec2 midq = p.UV[seg.localVerts[seg.localVerts.size() / 2]];
                Vec2 q = mm(pi, midq);
                os << "<text x=\"" << fmt(q.x()) << "\" y=\"" << fmt(q.y())
                   << "\" font-size=\"6\" fill=\"#c03030\">S" << seg.seamId
                   << "</text>\n";
            }
        }

        // panel label + dimensions
        os << "<text x=\"" << fmt(place[pi].offset.x() + place[pi].sizeMm.x() * 0.0)
           << "\" y=\"" << fmt(opt.marginMm - 6.0) << "\" font-size=\"7\" fill=\"#333333\">"
           << "panel " << p.id << " (" << p.label << ")</text>\n";
        if (opt.drawDimensions) {
            os << "<text x=\"" << fmt(place[pi].offset.x())
               << "\" y=\"" << fmt(opt.marginMm + maxH + 12.0)
               << "\" font-size=\"6\" fill=\"#333333\">"
               << fmt(place[pi].sizeMm.x()) << " x " << fmt(place[pi].sizeMm.y())
               << " mm</text>\n";
        }
        os << "</g>\n";
    }

    // seam relation summary
    if (!relations.empty()) {
        os << "<g id=\"seam-relations\" font-size=\"5\" fill=\"#555555\">\n";
        double y = H - 4.0;
        for (const auto& r : relations) {
            os << "<text x=\"" << fmt(opt.marginMm) << "\" y=\"" << fmt(y) << "\">S"
               << r.seamId << ": panel " << r.a.panelId << " &#8596; panel "
               << r.b.panelId << " conf=" << fmt(r.confidence)
               << " len-mismatch=" << fmt(r.lengthMismatch2d * 100) << "%"
               << (r.reversed ? " reversed" : "") << "</text>\n";
            y -= 6.0;
        }
        os << "</g>\n";
    }
    os << "</svg>\n";
    return os.str();
}

bool writeSvg(const std::string& path, const std::vector<Panel>& panels,
              const std::vector<std::vector<RegularizedLoop>>& regularized,
              const std::vector<SeamRelation>& relations,
              const ExportOptions& opt, std::string* err) {
    std::ofstream f(path);
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    f << renderSvg(panels, regularized, relations, opt);
    return bool(f);
}

bool writeDxf(const std::string& path, const std::vector<Panel>& panels,
              const std::vector<std::vector<RegularizedLoop>>& regularized,
              const ExportOptions& opt, std::string* err) {
    std::ofstream f(path);
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    f << "0\nSECTION\n2\nENTITIES\n";
    double xoff = 0;
    for (size_t pi = 0; pi < panels.size(); ++pi) {
        const Panel& p = panels[pi];
        Vec2 mn(1e300, 1e300), mx(-1e300, -1e300);
        for (const auto& q : p.UV) { mn = mn.cwiseMin(q); mx = mx.cwiseMax(q); }
        std::string layer = "PANEL_" + std::to_string(p.id);

        auto emitLoop = [&](const std::vector<Vec2>& pts) {
            f << "0\nPOLYLINE\n8\n" << layer << "\n66\n1\n70\n1\n";
            for (const auto& q0 : pts) {
                Vec2 q = (q0 - mn) * opt.unitsToMm;
                f << "0\nVERTEX\n8\n" << layer << "\n10\n" << fmt(q.x() + xoff)
                  << "\n20\n" << fmt(q.y()) << "\n30\n0.0\n";
            }
            f << "0\nSEQEND\n";
        };

        if (pi < regularized.size() && !regularized[pi].empty()) {
            for (const auto& reg : regularized[pi]) emitLoop(reg.simplified);
        } else {
            TriMesh pm = p.toTriMesh();
            for (const auto& loop : pm.boundaryLoops()) {
                std::vector<Vec2> pts;
                for (int v : loop) pts.push_back(p.UV[v]);
                emitLoop(pts);
            }
        }
        xoff += (mx - mn).x() * opt.unitsToMm + opt.spacingMm;
    }
    f << "0\nENDSEC\n0\nEOF\n";
    return bool(f);
}

} // namespace sf
