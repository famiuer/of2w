#include "coupling/MoorDynCApiAdapter.hpp"
#include "coupling/Math.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using oc4::coupling::ExternalWaveKinematicsProvider;
using oc4::coupling::FrameId;
using oc4::coupling::Matrix3;
using oc4::coupling::MoorDynApi;
using oc4::coupling::MoorDynCApiAdapter;
using oc4::coupling::MoorDynConfig;
using oc4::coupling::MoorDynHandle;
using oc4::coupling::PlatformState;
using oc4::coupling::ReferencePoint;
using oc4::coupling::Vector3;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "Test failure: " << message << '\n';
        std::exit(1);
    }
}

struct FakeMoorDynState {
    unsigned int nCoupledDof{6};
    unsigned int nWavePoints{0};
    std::vector<double> initX{};
    std::vector<double> initXd{};
    std::vector<double> stepX{};
    std::vector<double> stepXd{};
    std::vector<double> waveCoordinates{};
    std::vector<double> waveVelocity{};
    std::vector<double> waveAcceleration{};
    double waveTime{0.0};
    int createCount{0};
    int initCount{0};
    int initNoIcCount{0};
    int stepCount{0};
    int closeCount{0};
    int waveInitCount{0};
    int waveSetCount{0};
    int verbosity{-999};
    std::string logFilePath{};
    std::string inputFilePath{};
    std::array<double, 6> outputLoad{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
};

PlatformState makePlatformState()
{
    PlatformState state{};
    state.time = 5.0;
    state.referencePoint = {{1.0, 2.0, 3.0}, FrameId::global, "platformRef"};
    state.orientationGlobalFromBody = Matrix3::identity();
    state.positionGlobal = {1.0, 2.0, 3.0};
    state.velocityGlobal = {4.0, 5.0, 6.0};
    state.accelerationGlobal = {0.0, 0.0, 0.0};
    state.angularVelocityGlobal = {0.4, 0.5, 0.6};
    state.angularAccelerationGlobal = {0.0, 0.0, 0.0};
    state.eulerAnglesRad = {0.1, 0.2, 0.3};
    // Distinct sentinel: Euler-angle rates must NOT leak into the MoorDyn
    // velocity vector (MoorDyn expects angular velocity there).
    state.eulerAngleRatesRadPerSec = {7.7, 8.8, 9.9};
    return state;
}

MoorDynApi makeFakeApi(const std::shared_ptr<FakeMoorDynState>& fake)
{
    MoorDynApi api{};

    api.create = [fake](const char* inputFilePath) -> MoorDynHandle {
        fake->createCount += 1;
        fake->inputFilePath = inputFilePath != nullptr ? inputFilePath : "";
        return fake.get();
    };
    api.getNCoupledDof = [fake](MoorDynHandle, unsigned int* n) {
        *n = fake->nCoupledDof;
        return 0;
    };
    api.setVerbosity = [fake](MoorDynHandle, int verbosity) {
        fake->verbosity = verbosity;
        return 0;
    };
    api.setLogFile = [fake](MoorDynHandle, const char* logFilePath) {
        fake->logFilePath = logFilePath != nullptr ? logFilePath : "";
        return 0;
    };
    api.init = [fake](MoorDynHandle, const double* x, const double* xd) {
        fake->initCount += 1;
        fake->initX.assign(x, x + 6);
        fake->initXd.assign(xd, xd + 6);
        return 0;
    };
    api.initNoIc = [fake](MoorDynHandle, const double* x, const double* xd) {
        fake->initNoIcCount += 1;
        fake->initX.assign(x, x + 6);
        fake->initXd.assign(xd, xd + 6);
        return 0;
    };
    api.step = [fake](MoorDynHandle, const double* x, const double* xd, double* f, double*, double*) {
        fake->stepCount += 1;
        fake->stepX.assign(x, x + 6);
        fake->stepXd.assign(xd, xd + 6);
        for (std::size_t i = 0; i < fake->outputLoad.size(); ++i) {
            f[i] = fake->outputLoad[i];
        }
        return 0;
    };
    api.close = [fake](MoorDynHandle) {
        fake->closeCount += 1;
        return 0;
    };
    api.externalWaveKinInit = [fake](MoorDynHandle, unsigned int* n) {
        fake->waveInitCount += 1;
        *n = fake->nWavePoints;
        return 0;
    };
    api.externalWaveKinGetN = [fake](MoorDynHandle, unsigned int* n) {
        *n = fake->nWavePoints;
        return 0;
    };
    api.externalWaveKinGetCoordinates = [fake](MoorDynHandle, double* coordinates) {
        for (std::size_t i = 0; i < fake->waveCoordinates.size(); ++i) {
            coordinates[i] = fake->waveCoordinates[i];
        }
        return 0;
    };
    api.externalWaveKinSet = [fake](MoorDynHandle, const double* velocity, const double* acceleration, double time) {
        fake->waveSetCount += 1;
        fake->waveTime = time;
        fake->waveVelocity.assign(velocity, velocity + 3 * fake->nWavePoints);
        fake->waveAcceleration.assign(acceleration, acceleration + 3 * fake->nWavePoints);
        return 0;
    };

    return api;
}

void testSingleBodyMappingAndWaveCallback()
{
    auto fake = std::make_shared<FakeMoorDynState>();
    fake->nCoupledDof = 6;
    fake->nWavePoints = 2;
    fake->waveCoordinates = {
        -10.0, 0.0, 15.0,
        10.0, 0.0, 15.0,
    };
    fake->outputLoad = {100.0, 200.0, 300.0, 10.0, 20.0, 30.0};

    MoorDynConfig config{};
    config.inputFilePath = "Mooring/oc4_lines.txt";
    config.logFilePath = "logs/moordyn.log";
    config.verbosity = 2;
    config.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "oc4Ref"};
    config.externalWaveKinematicsProvider =
        [](double, const std::vector<Vector3>& samplePointsGlobal, std::vector<Vector3>& velocityGlobal, std::vector<Vector3>& accelerationGlobal) {
            velocityGlobal.clear();
            accelerationGlobal.clear();
            for (const auto& point : samplePointsGlobal) {
                velocityGlobal.push_back({point.x * 0.1, point.y * 0.1, point.z * 0.1});
                accelerationGlobal.push_back({point.x * 0.01, point.y * 0.01, point.z * 0.01});
            }
        };

    MoorDynCApiAdapter adapter(makeFakeApi(fake));
    const PlatformState initialState = makePlatformState();

    adapter.initialize(config, initialState);

    require(fake->createCount == 1, "create should be called once");
    require(fake->initCount == 1, "init should be called once");
    require(fake->initNoIcCount == 0, "initNoIc should not be called");
    require(fake->verbosity == 2, "verbosity should be forwarded");
    require(fake->logFilePath == "logs/moordyn.log", "log file path should be forwarded");
    require(fake->inputFilePath == "Mooring/oc4_lines.txt", "input path should be forwarded");
    require(fake->waveInitCount == 1, "external wave kinematics should be initialized once");
    require(fake->initX == std::vector<double>({1.0, 2.0, 3.0, 0.1, 0.2, 0.3}),
            "position state vector should map to x y z roll pitch yaw");
    require(fake->initXd == std::vector<double>({4.0, 5.0, 6.0, 0.4, 0.5, 0.6}),
            "velocity state vector should map to [vx vy vz wx wy wz] (angular velocity, not Euler-angle rates)");

    oc4::coupling::MoorDynResult result = adapter.step(initialState, 0.25);

    require(fake->stepCount == 1, "step should be called once");
    require(fake->waveSetCount == 1, "wave kinematics should be supplied once");
    require(fake->stepX == fake->initX, "step position vector should match expected ordering");
    require(fake->stepXd == fake->initXd, "step velocity vector should match expected ordering");
    require(fake->waveVelocity == std::vector<double>({-1.0, 0.0, 1.5, 1.0, 0.0, 1.5}),
            "wave velocities should be flattened in xyz order");
    require(fake->waveAcceleration == std::vector<double>({-0.1, 0.0, 0.15, 0.1, 0.0, 0.15}),
            "wave accelerations should be flattened in xyz order");
    require(oc4::coupling::nearlyEqual(result.resultantLoad.force, Vector3{100.0, 200.0, 300.0}, 1e-12),
            "result force should match MoorDyn output");
    require(oc4::coupling::nearlyEqual(result.resultantLoad.moment, Vector3{10.0, 20.0, 30.0}, 1e-12),
            "result moment should match MoorDyn output");
    require(result.resultantLoad.applicationPoint.name == "oc4Ref",
            "result reference point name should come from config");
    require(oc4::coupling::nearlyEqual(result.resultantLoad.applicationPoint.position, initialState.positionGlobal, 1e-12),
            "result application point position should follow the platform state");

    adapter.close();
    require(fake->closeCount == 1, "close should be called once");
}

void testSkipInitialConditionSolveUsesInitNoIc()
{
    auto fake = std::make_shared<FakeMoorDynState>();
    MoorDynConfig config{};
    config.inputFilePath = "Mooring/oc4_lines.txt";
    config.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "oc4Ref"};
    config.skipInitialConditionSolve = true;

    MoorDynCApiAdapter adapter(makeFakeApi(fake));
    adapter.initialize(config, makePlatformState());

    require(fake->initCount == 0, "init should not be called when skipInitialConditionSolve is true");
    require(fake->initNoIcCount == 1, "initNoIc should be called when skipInitialConditionSolve is true");
}

void testAppliesOpenFoamToMoorDynOffset()
{
    auto fake = std::make_shared<FakeMoorDynState>();
    MoorDynConfig config{};
    config.inputFilePath = "Mooring/oc4_lines.txt";
    config.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "oc4Ref"};
    config.openFoamToMoorDynOffsetGlobal = {0.0, 0.0, -15.0};

    MoorDynCApiAdapter adapter(makeFakeApi(fake));
    adapter.initialize(config, makePlatformState());
    adapter.step(makePlatformState(), 0.2);

    require(fake->initX == std::vector<double>({1.0, 2.0, -12.0, 0.1, 0.2, 0.3}),
            "position state vector should include the OpenFOAM-to-MoorDyn offset");
    require(fake->stepX == fake->initX,
            "step position vector should include the OpenFOAM-to-MoorDyn offset");
}

void testRejectsUnexpectedCoupledDof()
{
    auto fake = std::make_shared<FakeMoorDynState>();
    fake->nCoupledDof = 9;

    MoorDynConfig config{};
    config.inputFilePath = "Mooring/oc4_lines.txt";
    config.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "oc4Ref"};

    MoorDynCApiAdapter adapter(makeFakeApi(fake));

    bool threw = false;
    try {
        adapter.initialize(config, makePlatformState());
    } catch (const std::runtime_error&) {
        threw = true;
    }

    require(threw, "adapter should reject non-6DOF MoorDyn configurations in the v1 path");
}

void testRejectsMismatchedReferencePointPosition()
{
    auto fake = std::make_shared<FakeMoorDynState>();
    MoorDynConfig config{};
    config.inputFilePath = "Mooring/oc4_lines.txt";
    config.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "oc4Ref"};

    PlatformState badState = makePlatformState();
    badState.referencePoint.position = {9.0, 9.0, 9.0};

    MoorDynCApiAdapter adapter(makeFakeApi(fake));

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
    testSingleBodyMappingAndWaveCallback();
    testSkipInitialConditionSolveUsesInitNoIc();
    testAppliesOpenFoamToMoorDynOffset();
    testRejectsUnexpectedCoupledDof();
    testRejectsMismatchedReferencePointPosition();

    std::cout << "All MoorDyn adapter tests passed.\n";
    return 0;
}
