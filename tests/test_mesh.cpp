#include "core/Mesh.h"
#include "core/Procedural.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace sf;

TEST_CASE("skirt adjacency and boundary structure", "[mesh]") {
    SkirtOptions opt;
    GroundTruth gt;
    TriMesh m = makeSkirt(opt, &gt);

    REQUIRE(m.V.size() == size_t(opt.radialSegments * (opt.rings + 1)));
    REQUIRE(m.F.size() == size_t(2 * opt.radialSegments * opt.rings));
    REQUIRE(m.numNonManifoldEdges() == 0);

    // waist + hem
    auto loops = m.boundaryLoops();
    REQUIRE(loops.size() == 2);
    REQUIRE(loops[0].size() == size_t(opt.radialSegments));
    REQUIRE(loops[1].size() == size_t(opt.radialSegments));

    int nc = 0;
    m.faceComponents(nc);
    REQUIRE(nc == 1);
}

TEST_CASE("consistent outward winding", "[mesh]") {
    TriMesh m = makeSkirt({});
    // every face normal should point away from the axis (radially outward)
    for (int f = 0; f < (int)m.F.size(); ++f) {
        Vec3 c = (m.V[m.F[f][0]] + m.V[m.F[f][1]] + m.V[m.F[f][2]]) / 3.0;
        Vec3 radial(c.x(), 0, c.z());
        REQUIRE(m.faceNormal(f).dot(radial.normalized()) > 0.3);
    }
}

TEST_CASE("shortest vertex path follows edges", "[mesh]") {
    TriMesh m = makeSkirt({});
    auto path = shortestVertexPath(m, 0, (int)m.V.size() - 1);
    REQUIRE(path.size() >= 2);
    REQUIRE(path.front() == 0);
    REQUIRE(path.back() == (int)m.V.size() - 1);
    for (size_t i = 0; i + 1 < path.size(); ++i)
        REQUIRE(m.edgeBetween(path[i], path[i + 1]) >= 0);
}
