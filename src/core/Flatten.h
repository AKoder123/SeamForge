#pragma once
// Panel flattening (surface parameterisation) behind a replaceable interface.
//
// Implemented:
//  * LSCM  - least squares conformal maps (Levy et al. 2002). Fast linear
//            solve; minimises angle distortion; area distortion unbounded.
//  * ARAP  - as-rigid-as-possible parameterisation (Liu et al. 2008),
//            local/global iterations initialised from LSCM. Balances angle
//            and area distortion; near-isometric on developable panels.
//
// Post-processing normalises the 2D frame deterministically: no majority
// flip, principal axis vertical, min corner at origin. UV units equal the
// 3D units (metres), so seam lengths are directly comparable.

#include "Segmentation.h"
#include <memory>
#include <string>

namespace sf {

struct FlattenResult {
    bool success = false;
    std::string message;         // solver failure details when !success
    std::vector<Vec2> UV;        // per local panel vertex
    int flippedTriangles = 0;
    int iterations = 0;          // ARAP iterations actually run
    double finalEnergy = 0;
};

class IFlattener {
public:
    virtual ~IFlattener() = default;
    virtual FlattenResult flatten(const Panel& p) const = 0;
    virtual std::string name() const = 0;
};

class LSCMFlattener final : public IFlattener {
public:
    FlattenResult flatten(const Panel& p) const override;
    std::string name() const override { return "lscm"; }
};

class ARAPFlattener final : public IFlattener {
public:
    int maxIterations = 60;
    double relTolerance = 1e-7;
    FlattenResult flatten(const Panel& p) const override;
    std::string name() const override { return "arap"; }
};

std::unique_ptr<IFlattener> makeFlattener(const std::string& name);

// --- distortion ------------------------------------------------------------

struct FaceDistortion {
    double sigma1 = 1, sigma2 = 1;   // singular values of 3D->2D map, s1 >= s2
    bool flipped = false;
    double angleDistortion() const { return sigma2 > 1e-20 ? sigma1 / sigma2 : 1e20; }
    double areaRatio() const { return sigma1 * sigma2; }   // 1 = isometric
};

struct DistortionSummary {
    int flipped = 0;
    double meanAngleDistortion = 1;   // area-weighted mean of s1/s2 (>= 1)
    double maxAngleDistortion = 1;
    double meanAbsLogArea = 0;        // area-weighted mean |log(s1*s2)|
    double minAreaRatio = 1, maxAreaRatio = 1;
    double maxStretch = 1;            // max s1
    double maxCompression = 1;        // min s2
    double boundaryLengthChange = 0;  // max relative |2D-3D| boundary length error
};

std::vector<FaceDistortion> computeDistortion(const Panel& p, const std::vector<Vec2>& UV);
DistortionSummary summarizeDistortion(const Panel& p, const std::vector<Vec2>& UV,
                                      const std::vector<FaceDistortion>& fd);

} // namespace sf
