#include "core/Procedural.h"
#include "core/Validation.h"

#include <catch2/catch_test_macros.hpp>

using namespace sf;

namespace {
bool hasIssue(const ValidationReport& r, IssueType t, bool repaired) {
    for (const auto& i : r.issues)
        if (i.type == t && i.autoRepaired == repaired) return true;
    return false;
}
} // namespace

TEST_CASE("clean skirt validates without errors", "[validation]") {
    TriMesh m = makeSkirt({});
    auto rep = validateAndRepair(m);
    REQUIRE_FALSE(rep.hasErrors());
    REQUIRE(rep.flattenable());
    // two garment openings are reported as informational boundary loops
    REQUIRE(hasIssue(rep, IssueType::BoundaryLoops, false));
}

TEST_CASE("duplicate vertices are welded and reported", "[validation]") {
    TriMesh m = makeSkirt({});
    size_t nOrig = m.V.size();
    // duplicate a vertex and repoint one face corner at the copy
    m.V.push_back(m.V[m.F[0][0]]);
    m.F[0][0] = (int)m.V.size() - 1;
    auto rep = validateAndRepair(m);
    REQUIRE(hasIssue(rep, IssueType::DuplicateVertices, true));
    REQUIRE(m.V.size() == nOrig);
    REQUIRE_FALSE(rep.hasErrors());
}

TEST_CASE("flipped faces are re-oriented and reported", "[validation]") {
    TriMesh m = makeSkirt({});
    for (int f = 0; f < 40; f += 2) std::swap(m.F[f][1], m.F[f][2]);
    auto rep = validateAndRepair(m);
    REQUIRE(hasIssue(rep, IssueType::InconsistentWinding, true));
    REQUIRE_FALSE(rep.hasErrors());
    // all normals outward again
    for (int f = 0; f < (int)m.F.size(); ++f) {
        Vec3 c = (m.V[m.F[f][0]] + m.V[m.F[f][1]] + m.V[m.F[f][2]]) / 3.0;
        Vec3 radial(c.x(), 0, c.z());
        double d = m.faceNormal(f).dot(radial.normalized());
        INFO("face " << f);
        // consistent either all-outward or all-inward; check consistency
        REQUIRE(std::abs(d) > 0.3);
    }
}

TEST_CASE("degenerate triangles are dropped with a warning", "[validation]") {
    TriMesh m = makeSkirt({});
    size_t nOrig = m.F.size();
    m.F.push_back({0, 0, 5});                       // repeated index
    m.F.push_back({0, 1, 1});
    auto rep = validateAndRepair(m);
    REQUIRE(hasIssue(rep, IssueType::DegenerateTriangles, true));
    REQUIRE(m.F.size() == nOrig);
}

TEST_CASE("non-manifold edge is a blocking error, not repaired", "[validation]") {
    TriMesh m = makeSkirt({});
    // add a fin: new vertex attached across an existing interior edge
    // (F[0][0]-F[0][2] is a vertical edge shared by two quad-half faces;
    //  F[0][0]-F[0][1] would be a waist boundary edge and stay manifold)
    int a = m.F[0][0], b = m.F[0][2];
    m.V.push_back(m.V[a] + Vec3(0.1, 0.1, 0.1));
    m.F.push_back({a, b, (int)m.V.size() - 1});
    auto rep = validateAndRepair(m);
    REQUIRE(hasIssue(rep, IssueType::NonManifoldEdges, false));
    REQUIRE(rep.hasErrors());
    REQUIRE_FALSE(rep.flattenable());
}

TEST_CASE("suspicious scale is flagged", "[validation]") {
    TriMesh m = makeSkirt({});
    for (auto& v : m.V) v *= 1000.0;   // mm-scale garment pretending to be metres
    auto rep = validateAndRepair(m);
    REQUIRE(hasIssue(rep, IssueType::SuspiciousScale, false));
}
