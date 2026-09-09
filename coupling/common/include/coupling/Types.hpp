#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace oc4::coupling {

enum class FrameId {
    unknown,
    global,
    platformBody,
};

struct Vector3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct Matrix3 {
    std::array<double, 9> values{
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0,
    };

    static Matrix3 identity()
    {
        return {};
    }

    double& operator()(std::size_t row, std::size_t col)
    {
        return values.at(row * 3 + col);
    }

    double operator()(std::size_t row, std::size_t col) const
    {
        return values.at(row * 3 + col);
    }
};

struct ReferencePoint {
    Vector3 position{};
    FrameId frame{FrameId::global};
    std::string name{"unnamed"};
};

struct Wrench {
    Vector3 force{};
    Vector3 moment{};
    ReferencePoint applicationPoint{};
    FrameId expressedInFrame{FrameId::global};
};

struct PlatformState {
    double time{0.0};
    ReferencePoint referencePoint{};
    Matrix3 orientationGlobalFromBody{Matrix3::identity()};
    Vector3 positionGlobal{};
    Vector3 velocityGlobal{};
    Vector3 accelerationGlobal{};
    Vector3 angularVelocityGlobal{};
    Vector3 angularAccelerationGlobal{};
    Vector3 eulerAnglesRad{};
    Vector3 eulerAngleRatesRadPerSec{};
};

struct MoorDynResult {
    Wrench resultantLoad{};
    int statusCode{0};
    std::string statusMessage{};
    std::vector<double> fairleadTensions{};
};

struct OpenFASTResult {
    Wrench resultantLoad{};
    int statusCode{0};
    std::string statusMessage{};
    double rotorSpeedRadPerSec{0.0};
    double generatorPowerW{0.0};
    std::array<double, 3> bladePitchRad{0.0, 0.0, 0.0};
    Vector3 towerTopDisplacementGlobal{};
    Vector3 bladeTipDisplacementGlobal{};
    bool controllerOk{true};
};

}  // namespace oc4::coupling
