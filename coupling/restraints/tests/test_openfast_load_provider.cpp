#include "coupling/OpenFASTLoadProvider.hpp"
#include "coupling/Math.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using oc4::coupling::FrameId;
using oc4::coupling::Matrix3;
using oc4::coupling::OpenFASTAdapter;
using oc4::coupling::OpenFASTConfig;
using oc4::coupling::OpenFASTLoadProvider;
using oc4::coupling::OpenFASTLoadProviderConfig;
using oc4::coupling::OpenFASTResult;
using oc4::coupling::PlatformState;
using oc4::coupling::Vector3;
using oc4::coupling::Wrench;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "Test failure: " << message << '\n';
        std::exit(1);
    }
}

struct FakeOpenFASTAdapter final : public OpenFASTAdapter {
    OpenFASTConfig config{};
    PlatformState initializedState{};
    PlatformState steppedState{};
    OpenFASTResult nextResult{};
    bool initializeCalled{false};
    bool closeCalled{false};
    int stepCount{0};

    void initialize(const OpenFASTConfig& inConfig, const PlatformState& initialState) override
    {
        config = inConfig;
        initializedState = initialState;
        initializeCalled = true;
    }

    OpenFASTResult step(const PlatformState& platformState, double) override
    {
        steppedState = platformState;
        stepCount += 1;
        return nextResult;
    }

    void close() override
    {
        closeCalled = true;
    }
};

PlatformState makePlatformState()
{
    PlatformState state{};
    state.time = 9.0;
    state.referencePoint = {{1.0, 2.0, 3.0}, FrameId::global, "platformRef"};
    state.orientationGlobalFromBody = Matrix3::identity();
    state.positionGlobal = {1.0, 2.0, 3.0};
    state.velocityGlobal = {4.0, 5.0, 6.0};
    state.eulerAnglesRad = {0.1, 0.2, 0.3};
    state.eulerAngleRatesRadPerSec = {0.4, 0.5, 0.6};
    return state;
}

void testShiftAndAdapterDelegation()
{
    auto adapter = std::make_unique<FakeOpenFASTAdapter>();
    FakeOpenFASTAdapter* rawAdapter = adapter.get();

    OpenFASTLoadProviderConfig config{};
    config.openFast.casePath = "external/openfast/cases/oc4_smoke";
    config.openFast.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "oc4Ref"};
    config.openFast.turbineInterfacePoint = {{0.0, 0.0, 10.0}, FrameId::global, "towerBase"};
    config.shiftResultToPlatformReferencePoint = true;

    rawAdapter->nextResult.resultantLoad.force = {0.0, 20.0, 0.0};
    rawAdapter->nextResult.resultantLoad.moment = {0.0, 0.0, 0.0};
    rawAdapter->nextResult.resultantLoad.applicationPoint = {{1.0, 2.0, 4.0}, FrameId::global, "towerBase"};
    rawAdapter->nextResult.resultantLoad.expressedInFrame = FrameId::global;
    rawAdapter->nextResult.statusCode = 0;
    rawAdapter->nextResult.statusMessage = "ok";

    OpenFASTLoadProvider provider(std::move(config), std::move(adapter));
    const PlatformState state = makePlatformState();
    provider.initialize(state);

    require(rawAdapter->initializeCalled, "provider should initialize the wrapped adapter");
    require(rawAdapter->initializedState.referencePoint.name == "platformRef",
            "initial state should be forwarded to OpenFAST adapter");

    const Wrench load = provider.evaluate(state, 0.25);

    require(rawAdapter->stepCount == 1, "provider should step the adapter exactly once");
    require(oc4::coupling::nearlyEqual(load.force, Vector3{0.0, 20.0, 0.0}, 1e-12),
            "provider should preserve the OpenFAST force");
    require(oc4::coupling::nearlyEqual(load.moment, Vector3{-20.0, 0.0, 0.0}, 1e-12),
            "provider should shift the interface load onto the platform reference point");
    require(load.applicationPoint.name == "platformRef",
            "shifted load should be referenced to the platform point");

    provider.close();
    require(rawAdapter->closeCalled, "provider close should close the adapter");
}

void testDebugCsvOutput()
{
    auto adapter = std::make_unique<FakeOpenFASTAdapter>();
    FakeOpenFASTAdapter* rawAdapter = adapter.get();

    const std::filesystem::path csvPath =
        std::filesystem::temp_directory_path() / "test_openfast_load_provider.csv";

    OpenFASTLoadProviderConfig config{};
    config.openFast.casePath = "external/openfast/cases/oc4_smoke";
    config.openFast.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "oc4Ref"};
    config.openFast.turbineInterfacePoint = {{0.0, 0.0, 10.0}, FrameId::global, "towerBase"};
    config.shiftResultToPlatformReferencePoint = false;
    config.debugCsvPath = csvPath.string();

    rawAdapter->nextResult.resultantLoad.force = {7.0, 8.0, 9.0};
    rawAdapter->nextResult.resultantLoad.moment = {1.0, 2.0, 3.0};
    rawAdapter->nextResult.resultantLoad.applicationPoint = {{1.0, 2.0, 3.0}, FrameId::global, "towerBase"};
    rawAdapter->nextResult.resultantLoad.expressedInFrame = FrameId::global;
    rawAdapter->nextResult.statusCode = 4;
    rawAdapter->nextResult.statusMessage = "openfast-row";
    rawAdapter->nextResult.rotorSpeedRadPerSec = 1.23;
    rawAdapter->nextResult.generatorPowerW = 456.0;
    rawAdapter->nextResult.controllerOk = true;

    OpenFASTLoadProvider provider(std::move(config), std::move(adapter));
    const PlatformState state = makePlatformState();
    provider.initialize(state);
    provider.evaluate(state, 0.125);
    provider.close();

    std::ifstream input(csvPath);
    require(input.is_open(), "debug CSV should be created");

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();

    require(text.find("rotor_speed_rad_s,generator_power_w") != std::string::npos,
            "CSV header should include OpenFAST diagnostics");
    require(text.find("openfast-row") != std::string::npos,
            "CSV row should contain the OpenFAST status message");
    require(text.find("456") != std::string::npos,
            "CSV row should contain generator power");

    std::filesystem::remove(csvPath);
}

void testEvaluateRequiresInitialize()
{
    auto adapter = std::make_unique<FakeOpenFASTAdapter>();

    OpenFASTLoadProviderConfig config{};
    config.openFast.casePath = "external/openfast/cases/oc4_smoke";
    config.openFast.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "oc4Ref"};
    config.openFast.turbineInterfacePoint = {{0.0, 0.0, 10.0}, FrameId::global, "towerBase"};

    OpenFASTLoadProvider provider(std::move(config), std::move(adapter));

    bool threw = false;
    try {
        provider.evaluate(makePlatformState(), 0.1);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    require(threw, "provider should reject evaluate before initialize");
}

}  // namespace

int main()
{
    testShiftAndAdapterDelegation();
    testDebugCsvOutput();
    testEvaluateRequiresInitialize();

    std::cout << "All OpenFAST load-provider tests passed.\n";
    return 0;
}
