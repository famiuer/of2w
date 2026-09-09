#include "coupling/ForcedOpenFASTCApiAdapter.hpp"
#include "coupling/Math.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using oc4::coupling::ForcedOpenFASTCApiAdapter;
using oc4::coupling::FrameId;
using oc4::coupling::Matrix3;
using oc4::coupling::OpenFASTApi;
using oc4::coupling::OpenFASTConfig;
using oc4::coupling::OpenFASTHandle;
using oc4::coupling::PlatformState;
using oc4::coupling::Vector3;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "Test failure: " << message << '\n';
        std::exit(1);
    }
}

struct FakeOpenFASTState {
    std::vector<double> initializeDisplacement{};
    std::vector<double> initializeVelocity{};
    std::vector<double> initializeAcceleration{};
    std::vector<double> stepDisplacement{};
    std::vector<double> stepVelocity{};
    std::vector<double> stepAcceleration{};
    std::vector<double> applicationPoint{1.0, 2.0, 3.0};
    std::vector<double> force{10.0, 20.0, 30.0};
    std::vector<double> moment{1.0, 2.0, 3.0};
    std::vector<double> bladePitch{0.1, 0.2, 0.3};
    std::vector<double> towerTopDisplacement{4.0, 5.0, 6.0};
    double rotorSpeedRadPerSec{7.0};
    double generatorPowerW{8.0};
    int controllerOk{1};
    std::string casePath{};
    int createCount{0};
    int initializeCount{0};
    int stepCount{0};
    int closeCount{0};
};

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

OpenFASTApi makeFakeApi(const std::shared_ptr<FakeOpenFASTState>& fake)
{
    OpenFASTApi api{};

    api.create = [fake](const char* casePath) -> OpenFASTHandle {
        fake->createCount += 1;
        fake->casePath = casePath != nullptr ? casePath : "";
        return fake.get();
    };
    api.initialize = [fake](OpenFASTHandle, const double* displacement, const double* velocity, const double* acceleration) {
        fake->initializeCount += 1;
        fake->initializeDisplacement.assign(displacement, displacement + 6);
        fake->initializeVelocity.assign(velocity, velocity + 6);
        fake->initializeAcceleration.assign(acceleration, acceleration + 6);
        return 0;
    };
    api.step = [fake](OpenFASTHandle, const double* displacement, const double* velocity, const double* acceleration, double*, double*) {
        fake->stepCount += 1;
        fake->stepDisplacement.assign(displacement, displacement + 6);
        fake->stepVelocity.assign(velocity, velocity + 6);
        fake->stepAcceleration.assign(acceleration, acceleration + 6);
        return 0;
    };
    api.getTowerBaseLoad = [fake](OpenFASTHandle, double* applicationPoint, double* force, double* moment) {
        for (int i = 0; i < 3; ++i) {
            applicationPoint[i] = fake->applicationPoint[static_cast<std::size_t>(i)];
            force[i] = fake->force[static_cast<std::size_t>(i)];
            moment[i] = fake->moment[static_cast<std::size_t>(i)];
        }
        return 0;
    };
    api.getRotorSpeed = [fake](OpenFASTHandle, double* rotorSpeedRadPerSec) {
        *rotorSpeedRadPerSec = fake->rotorSpeedRadPerSec;
        return 0;
    };
    api.getGeneratorPower = [fake](OpenFASTHandle, double* generatorPowerW) {
        *generatorPowerW = fake->generatorPowerW;
        return 0;
    };
    api.getBladePitch = [fake](OpenFASTHandle, double* bladePitchRad) {
        for (int i = 0; i < 3; ++i) {
            bladePitchRad[i] = fake->bladePitch[static_cast<std::size_t>(i)];
        }
        return 0;
    };
    api.getTowerTopDisplacement = [fake](OpenFASTHandle, double* towerTopDisplacementGlobal) {
        for (int i = 0; i < 3; ++i) {
            towerTopDisplacementGlobal[i] = fake->towerTopDisplacement[static_cast<std::size_t>(i)];
        }
        return 0;
    };
    api.getControllerStatus = [fake](OpenFASTHandle, int* controllerOk) {
        *controllerOk = fake->controllerOk;
        return 0;
    };
    api.close = [fake](OpenFASTHandle) {
        fake->closeCount += 1;
        return 0;
    };

    return api;
}

void testMappingAndDiagnostics()
{
    auto fake = std::make_shared<FakeOpenFASTState>();

    OpenFASTConfig config{};
    config.casePath = "external/openfast/cases/oc4_smoke";
    config.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "oc4Ref"};
    config.turbineInterfacePoint = {{0.0, 0.0, 10.0}, FrameId::global, "towerBase"};
    config.openFoamToOpenFastOffsetGlobal = {0.0, 0.0, -15.0};

    ForcedOpenFASTCApiAdapter adapter(makeFakeApi(fake));
    const PlatformState initialState = makePlatformState();
    adapter.initialize(config, initialState);

    require(fake->createCount == 1, "create should be called once");
    require(fake->initializeCount == 1, "initialize should be called once");
    require(fake->casePath == "external/openfast/cases/oc4_smoke", "case path should be forwarded");
    require(fake->initializeDisplacement == std::vector<double>({1.0, 2.0, -12.0, 0.1, 0.2, 0.3}),
            "displacement vector should include XYZ Euler angles and frame offset");
    require(fake->initializeVelocity == std::vector<double>({4.0, 5.0, 6.0, 0.11, 0.22, 0.33}),
            "velocity vector should include linear and angular velocity");
    require(fake->initializeAcceleration == std::vector<double>({0.7, 0.8, 0.9, 0.44, 0.55, 0.66}),
            "acceleration vector should include linear and angular acceleration");

    const oc4::coupling::OpenFASTResult result = adapter.step(initialState, 0.25);

    require(fake->stepCount == 1, "step should be called once");
    require(fake->stepDisplacement == fake->initializeDisplacement,
            "step displacement should match expected mapping");
    require(fake->stepVelocity == fake->initializeVelocity,
            "step velocity should match expected mapping");
    require(fake->stepAcceleration == fake->initializeAcceleration,
            "step acceleration should match expected mapping");
    require(oc4::coupling::nearlyEqual(result.resultantLoad.force, Vector3{10.0, 20.0, 30.0}, 1e-12),
            "returned force should match wrapper output");
    require(oc4::coupling::nearlyEqual(result.resultantLoad.moment, Vector3{1.0, 2.0, 3.0}, 1e-12),
            "returned moment should match wrapper output");
    require(oc4::coupling::nearlyEqual(result.resultantLoad.applicationPoint.position, Vector3{1.0, 2.0, 18.0}, 1e-12),
            "application point should be mapped back into OpenFOAM coordinates");
    require(result.rotorSpeedRadPerSec == 7.0, "rotor speed should be forwarded");
    require(result.generatorPowerW == 8.0, "generator power should be forwarded");
    require(oc4::coupling::nearlyEqual(result.towerTopDisplacementGlobal, Vector3{4.0, 5.0, 6.0}, 1e-12),
            "tower-top displacement should be forwarded");
    require(result.controllerOk, "controller status should be forwarded");

    adapter.close();
    require(fake->closeCount == 1, "close should be called once");
}

void testRejectsMismatchedReferencePointPosition()
{
    auto fake = std::make_shared<FakeOpenFASTState>();
    OpenFASTConfig config{};
    config.casePath = "external/openfast/cases/oc4_smoke";
    config.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "oc4Ref"};
    config.turbineInterfacePoint = {{0.0, 0.0, 10.0}, FrameId::global, "towerBase"};

    PlatformState badState = makePlatformState();
    badState.referencePoint.position = {9.0, 9.0, 9.0};

    ForcedOpenFASTCApiAdapter adapter(makeFakeApi(fake));

    bool threw = false;
    try {
        adapter.initialize(config, badState);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    require(threw, "adapter should reject mismatched platform positions");
}

}  // namespace

int main()
{
    testMappingAndDiagnostics();
    testRejectsMismatchedReferencePointPosition();

    std::cout << "All ForcedOpenFAST adapter tests passed.\n";
    return 0;
}
