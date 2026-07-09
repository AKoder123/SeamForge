#pragma once
// Mesh validation and conservative repair.
//
// Policy: destructive defects are reported, never silently repaired.
// Each repair that IS performed automatically (welding exact duplicates,
// re-orienting winding, dropping zero-area faces) is listed in the report
// with autoRepaired = true so the user can audit what changed.

#include "Mesh.h"
#include <string>
#include <vector>

namespace sf {

enum class IssueType {
    DisconnectedComponents,
    DuplicateVertices,
    UnreferencedVertices,
    DegenerateTriangles,
    ThinTriangles,
    InconsistentWinding,
    NonOrientable,
    NonManifoldEdges,
    BoundaryLoops,       // expected for garments (waist/hem) - informational
    SmallHoles,          // suspiciously small boundary loops - likely defects
    SuspiciousScale,
    Orientation,
};

enum class Severity { Info, Warning, Error };

struct Issue {
    IssueType type;
    Severity severity = Severity::Info;
    int count = 0;
    bool autoRepaired = false;
    std::string description;
};

struct ValidationReport {
    std::vector<Issue> issues;
    bool hasErrors() const;
    bool flattenable() const { return !hasErrors(); }
    std::string toText() const;
};

struct ValidationOptions {
    bool weldDuplicates = true;        // weld vertices closer than weldEpsilonRel * bboxDiag
    double weldEpsilonRel = 1e-7;
    bool dropDegenerateFaces = true;   // faces with repeated indices or ~zero area
    double degenerateAreaRel = 1e-12;  // relative to mean face area
    bool fixWinding = true;            // re-orient faces for consistent winding (if orientable)
    double thinTriangleAspect = 25.0;  // report-only threshold
    double smallHoleFrac = 0.05;       // loops shorter than frac * largest loop => suspected hole
    double expectedMinDiag = 0.05;     // metres; outside [min,max] scale is flagged
    double expectedMaxDiag = 100.0;
};

// Validates and (where safe) repairs `m` in place. Rebuilds adjacency.
ValidationReport validateAndRepair(TriMesh& m, const ValidationOptions& opt = {});

const char* issueTypeName(IssueType t);

} // namespace sf
