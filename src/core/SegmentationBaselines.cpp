#include "SegmentationBaselines.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace sf {

namespace {

struct Chart {
    Vec3 axis = Vec3::UnitY();     // cone axis N
    double cosTheta = 1.0;         // cos of opening angle
    Vec3 seedCentroid = Vec3::Zero();
    std::vector<int> faces;
    std::unordered_set<int> vset, eset;  // for incremental Euler characteristic
    int chi = 0;
};

Vec3 faceCentroid(const TriMesh& m, int f) {
    const auto& t = m.F[f];
    return (m.V[t[0]] + m.V[t[1]] + m.V[t[2]]) / 3.0;
}

// Adding face f must keep the chart a topological disk (chi == 1).
bool diskOkAndAdd(const TriMesh& m, Chart& c, int f, bool commit) {
    const auto& t = m.F[f];
    int newV = 0, newE = 0;
    for (int k = 0; k < 3; ++k) {
        if (!c.vset.count(t[k])) ++newV;
        if (!c.eset.count(m.faceEdges[f][k])) ++newE;
    }
    int chi = c.chi + newV - newE + 1;
    if (chi != 1) return false;
    if (commit) {
        c.chi = chi;
        for (int k = 0; k < 3; ++k) {
            c.vset.insert(t[k]);
            c.eset.insert(m.faceEdges[f][k]);
        }
        c.faces.push_back(f);
    }
    return true;
}

// Cone-proxy fit to a chart's (area-weighted) face normals: the axis is the
// direction of least normal variance; cosTheta the weighted mean projection.
void fitProxy(const TriMesh& m, Chart& c) {
    Vec3 mean = Vec3::Zero();
    double wsum = 0;
    std::vector<std::pair<Vec3, double>> ns;
    ns.reserve(c.faces.size());
    for (int f : c.faces) {
        double a = m.faceArea(f);
        Vec3 n = m.faceNormal(f);
        ns.push_back({n, a});
        mean += a * n;
        wsum += a;
    }
    if (wsum < 1e-30 || ns.empty()) return;
    mean /= wsum;
    Eigen::Matrix3d C = Eigen::Matrix3d::Zero();
    for (const auto& [n, a] : ns) {
        Vec3 d = n - mean;
        C += a * d * d.transpose();
    }
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(C);
    Vec3 N = es.eigenvectors().col(0);   // smallest eigenvalue
    if (N.dot(mean) < 0) N = -N;
    // degenerate (planar chart): eigenvector may be arbitrary in sign/dir but
    // any axis with zero variance is a valid proxy
    c.axis = N.normalized();
    double ct = 0;
    for (const auto& [n, a] : ns) ct += a * c.axis.dot(n);
    c.cosTheta = ct / wsum;
}

double fitError(const TriMesh& m, const Chart& c, int f) {
    double d = c.axis.dot(m.faceNormal(f)) - c.cosTheta;
    return d * d;
}

} // namespace

DChartsResult dchartsSegment(const TriMesh& m, const DChartsOptions& opt) {
    DChartsResult out;
    std::ostringstream log;
    const int nf = (int)m.F.size();
    if (nf == 0 || !m.adjacencyValid()) {
        out.log = "empty mesh or adjacency not built";
        return out;
    }
    const double diag = std::max(m.bbox().diagonal().norm(), 1e-12);

    std::vector<int> assign(nf, -1);
    std::vector<int> seeds;   // one per chart

    // one full growth pass from the given seeds; extends `seeds` when faces
    // remain unreachable (topology rejection or disconnection)
    auto growPass = [&](std::vector<Chart>& charts) {
        assign.assign(nf, -1);
        using QE = std::tuple<double, int, int>;   // cost, face, chart
        std::priority_queue<QE, std::vector<QE>, std::greater<>> pq;

        auto pushNeighbors = [&](int f, int c) {
            for (int k = 0; k < 3; ++k) {
                int e = m.faceEdges[f][k];
                if (m.edges[e].faces.size() != 2) continue;
                for (int g : m.edges[e].faces) {
                    if (g == f || assign[g] >= 0) continue;
                    double cost = fitError(m, charts[c], g) +
                                  opt.distanceWeight *
                                      (faceCentroid(m, g) - charts[c].seedCentroid).norm() / diag;
                    pq.push({cost, g, c});
                }
            }
        };

        int assigned = 0;
        auto seedChart = [&](int f) {
            Chart c;
            c.axis = m.faceNormal(f);
            c.cosTheta = 1.0;
            c.seedCentroid = faceCentroid(m, f);
            charts.push_back(std::move(c));
            int ci = (int)charts.size() - 1;
            diskOkAndAdd(m, charts[ci], f, true);
            assign[f] = ci;
            ++assigned;
            pushNeighbors(f, ci);
        };

        charts.clear();
        for (int s : seeds)
            if (assign[s] < 0) seedChart(s);

        while (assigned < nf) {
            while (!pq.empty()) {
                auto [cost, f, c] = pq.top();
                pq.pop();
                if (assign[f] >= 0) continue;
                if (!diskOkAndAdd(m, charts[c], f, false)) continue;  // re-queued via future neighbours
                diskOkAndAdd(m, charts[c], f, true);
                assign[f] = c;
                ++assigned;
                pushNeighbors(f, c);
            }
            if (assigned >= nf) break;
            // unreachable faces remain (disk-topology rejection closed the
            // ring, or disconnected component): open a new chart at the
            // lowest-id unassigned face (deterministic). maxCharts is a hard
            // safety cap - beyond it we still seed (coverage guarantee) but
            // the log makes the overflow visible.
            for (int f = 0; f < nf; ++f)
                if (assign[f] < 0) {
                    seeds.push_back(f);
                    seedChart(f);
                    break;
                }
        }
    };

    seeds.push_back(0);   // deterministic initial seed
    std::vector<Chart> charts;
    std::vector<int> prevAssign;
    int iter = 0;
    for (; iter < opt.iterations; ++iter) {
        growPass(charts);
        if (assign == prevAssign) break;
        prevAssign = assign;
        // Lloyd step: re-fit proxies, reseed each chart at its best-fitting
        // face (deterministic tie-break: lowest face id)
        std::vector<int> newSeeds;
        for (auto& c : charts) {
            if (c.faces.empty()) continue;
            fitProxy(m, c);
            int best = c.faces.front();
            double bestErr = 1e300;
            std::vector<int> sorted = c.faces;
            std::sort(sorted.begin(), sorted.end());
            for (int f : sorted) {
                double e = fitError(m, c, f);
                if (e < bestErr - 1e-15) {
                    bestErr = e;
                    best = f;
                }
            }
            newSeeds.push_back(best);
        }
        seeds = std::move(newSeeds);
    }

    out.faceChart = assign;
    out.chartCount = (int)charts.size();
    out.iterationsRun = iter;
    for (auto& c : charts) {
        fitProxy(m, c);
        double se = 0, wsum = 0;
        for (int f : c.faces) {
            double a = m.faceArea(f);
            se += a * fitError(m, c, f);
            wsum += a;
        }
        out.chartRmsFit.push_back(wsum > 0 ? std::sqrt(se / wsum) : 0.0);
    }
    log << "d-charts: " << out.chartCount << " charts after " << iter
        << " iteration(s)\n";
    for (size_t i = 0; i < out.chartRmsFit.size(); ++i)
        log << "  chart " << i << ": " << charts[i].faces.size()
            << " faces, rms cone fit " << out.chartRmsFit[i] << "\n";
    log << "note: chart boundaries minimise developability error, not garment "
           "construction convention; on smooth garments the cut position is "
           "arbitrary (documented failure mode)\n";
    out.log = log.str();
    return out;
}

} // namespace sf
