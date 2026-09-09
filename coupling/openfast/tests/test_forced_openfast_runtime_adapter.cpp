#include "coupling/ForcedOpenFASTCApiAdapter.hpp"
#include "coupling/Math.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using oc4::coupling::ForcedOpenFASTCApiAdapter;
using oc4::coupling::FrameId;
using oc4::coupling::Matrix3;
using oc4::coupling::OpenFASTConfig;
using oc4::coupling::PlatformState;
using oc4::coupling::Vector3;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "Test failure: " << message << '\n';
        std::exit(1);
    }
}

PlatformState makePlatformState()
{
    PlatformState state{};
    state.time = 5.0;
    state.referencePoint = {{1.0, 2.0, 3.0}, FrameId::global, "platformRef"};
    state.orientationGlobalFromBody = Matrix3::identity();
    state.positionGlobal = {1.0, 2.0, 3.0};
    state.velocityGlobal = {4.0, 5.0, 6.0};
    state.accelerationGlobal = {0.7, 0.8, 0.9};
    state.angularVelocityGlobal = {0.11, 0.22, 0.33};
    state.angularAccelerationGlobal = {0.44, 0.55, 0.66};
    state.eulerAnglesRad = {0.1, 0.2, 0.3};
    state.eulerAngleRatesRadPerSec = {0.4, 0.5, 0.6};
    return state;
}

}  // namespace

int main(int argc, char** argv)
{
    require(argc == 3, "expected smoke-case path and wrapper-library path arguments");

    OpenFASTConfig config{};
    config.casePath = argv[1];
    config.libraryPath = argv[2];
    config.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "oc4Ref"};
    config.turbineInterfacePoint = {{0.0, 0.0, 10.0}, FrameId::global, "towerBase"};
    config.openFoamToOpenFastOffsetGlobal = {0.0, 0.0, -15.0};

    ForcedOpenFASTCApiAdapter adapter;
    const PlatformState state = makePlatformState();
    adapter.initialize(config, state);
    const oc4::coupling::OpenFASTResult result = adapter.step(state, 0.25);
    adapter.close();

    require(oc4::coupling::nearlyEqual(result.resultantLoad.force, Vector3{-35000.0, -62500.0, 171000.0}, 1e-12),
            "adapter force mismatch");
    require(oc4::coupling::nearlyEqual(result.resultantLoad.moment, Vector3{-555000.0, -1110000.0, -832500.0}, 1e-12),
            "adapter moment mismatch");
    require(
        oc4::coupling::nearlyEqual(result.resultantLoad.applicationPoint.position, Vector3{0.0, 0.0, 10.0}, 1e-12),
        "adapter application point should map back into OpenFOAM frame");
    require(result.controllerOk, "adapter should return healthy controller status");
    require(std::fabs(result.rotorSpeedRadPerSec - 1.04) < 1e-12, "adapter rotor speed mismatch");
    require(std::fabs(result.generatorPowerW - 12000.0) < 1e-12, "adapter generator power mismatch");
    require(
        oc4::coupling::nearlyEqual(result.towerTopDisplacementGlobal, Vector3{0.1, 0.2, -1.2}, 1e-12),
        "adapter tower-top displacement mismatch");

    std::cout << "All ForcedOpenFAST runtime-adapter tests passed.\n";
    return 0;
}
