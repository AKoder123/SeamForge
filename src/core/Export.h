#pragma once
// Pattern export: SVG (primary) and minimal DXF R12 polylines.
// Output is deterministic (fixed precision, stable ordering) so exports are
// reproducible and diffable.

#include "Flatten.h"
#include "Regularize.h"
#include "Relations.h"
#include "Segmentation.h"
#include <string>
#include <vector>

namespace sf {

struct ExportOptions {
    double unitsToMm = 1000.0;      // mesh units (metres) -> millimetres
    bool drawRawBoundary = true;    // light raw polyline under the clean outline
    bool drawHeatmap = false;       // per-triangle angle-distortion fill
    bool drawSeamLabels = true;
    bool drawDimensions = true;
    double marginMm = 20.0;
    double spacingMm = 30.0;
};

// Renders all panels side by side. `regularized` may be empty (raw only);
// when present it must hold one entry per panel boundary loop in order.
std::string renderSvg(const std::vector<Panel>& panels,
                      const std::vector<std::vector<RegularizedLoop>>& regularized,
                      const std::vector<SeamRelation>& relations,
                      const ExportOptions& opt = {});

bool writeSvg(const std::string& path, const std::vector<Panel>& panels,
              const std::vector<std::vector<RegularizedLoop>>& regularized,
              const std::vector<SeamRelation>& relations,
              const ExportOptions& opt = {}, std::string* err = nullptr);

// Minimal DXF R12: one closed POLYLINE per panel boundary loop, one layer
// per panel. Uses the regularised outline when available.
bool writeDxf(const std::string& path, const std::vector<Panel>& panels,
              const std::vector<std::vector<RegularizedLoop>>& regularized,
              const ExportOptions& opt = {}, std::string* err = nullptr);

} // namespace sf
