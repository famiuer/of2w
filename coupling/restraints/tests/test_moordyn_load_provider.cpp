#include "coupling/MoorDynLoadProvider.hpp"
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
using oc4::coupling::MoorDynAdapter;
using oc4::coupling::MoorDynConfig;
using oc4::coupling::MoorDynLoadProvider;
using oc4::coupling::MoorDynLoadProviderConfig;
using oc4::coupling::MoorDynResult;
using oc4::coupling::PlatformState;
using oc4::coupling::ReferencePoint;
using oc4::coupling::Vector3;
using oc4::coupling::Wrench;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "Test failure: " << message << '\n';
        std::exit(1);
    }
}

struct FakeMoorDynAdapter final : public MoorDynAdapter {
    MoorDynConfig config{};
    PlatformState initializedState{};
    PlatformState steppedState{};
    MoorDynResult nextResult{};
    bool initializeCalled{false};
    bool closeCalled{false};
    int stepCount{0};

    void initialize(const MoorDynConfig& inConfig, const PlatformState& initialState) override
    {
        config = inConfig;
        initializedState = initialState;
        initializeCalled = true;
    }

    MoorDynResult step(const PlatformState& platformState, double) override
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
    state.time = 8.0;
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
    auto adapter = std::make_unique<FakeMoorDynAdapter>();
    FakeMoorDynAdapter* rawAdapter = adapter.get();

    MoorDynLoadProviderConfig config{};
    config.moorDyn.inputFilePath = "Mooring/oc4_lines.txt";
    config.moorDyn.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "commonRef"};
    config.shiftResultToPlatformReferencePoint = true;

    rawAdapter->nextResult.resultantLoad.force = {10.0, 0.0, 0.0};
    rawAdapter->nextResult.resultantLoad.moment = {0.0, 0.0, 0.0};
    rawAdapter->nextResult.resultantLoad.applicationPoint = {{1.0, 2.0, 4.0}, FrameId::global, "mdPoint"};
    rawAdapter->nextResult.resultantLoad.expressedInFrame = FrameId::global;
    rawAdapter->nextResult.statusCode = 0;
    rawAdapter->nextResult.statusMessage = "ok";

    MoorDynLoadProvider provider(std::move(config), std::move(adapter));
    const PlatformState state = makePlatformState();
    provider.initialize(state);

    require(rawAdapter->initializeCalled, "provider should initialize the wrapped adapter");
    require(rawAdapter->initializedState.referencePoint.name == "platformRef",
            "initial state should be forwarded to adapter");

    const Wrench load = provider.evaluate(state, 0.25);

    require(rawAdapter->stepCount == 1, "provider should step the adapter exactly once");
    require(oc4::coupling::nearlyEqual(load.force, Vector3{10.0, 0.0, 0.0}, 1e-12),
            "provider should preserve the force");
    require(oc4::coupling::nearlyEqual(load.moment, Vector3{0.0, 10.0, 0.0}, 1e-12),
            "provider should shift the moment onto the platform reference point");
    require(load.applicationPoint.name == "platformRef",
            "shifted load should be referenced to the platform point");

    provider.close();
    require(rawAdapter->closeCalled, "provider close should close the adapter");
}

void testDebugCsvOutput()
{
    auto adapter = std::make_unique<FakeMoorDynAdapter>();
    FakeMoorDynAdapter* rawAdapter = adapter.get();

    const std::filesystem::path csvPath =
        std::filesystem::temp_directory_path() / "test_moordyn_load_provider.csv";

    MoorDynLoadProviderConfig config{};
    config.moorDyn.inputFilePath = "Mooring/oc4_lines.txt";
    config.moorDyn.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "commonRef"};
    config.shiftResultToPlatformReferencePoint = false;
    config.debugCsvPath = csvPath.string();

    rawAdapter->nextResult.resultantLoad.force = {7.0, 8.0, 9.0};
    rawAdapter->nextResult.resultantLoad.moment = {1.0, 2.0, 3.0};
    rawAdapter->nextResult.resultantLoad.applicationPoint = {{1.0, 2.0, 3.0}, FrameId::global, "samePoint"};
    rawAdapter->nextResult.resultantLoad.expressedInFrame = FrameId::global;
    rawAdapter->nextResult.statusCode = 17;
    rawAdapter->nextResult.statusMessage = "debug-row";

    MoorDynLoadProvider provider(std::move(config), std::move(adapter));
    const PlatformState state = makePlatformState();
    provider.initialize(state);
    provider.evaluate(state, 0.125);
    provider.close();

    std::ifstream input(csvPath);
    require(input.is_open(), "debug CSV should be created");

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();

    require(text.find("time,dt,platform_ref_x") != std::string::npos,
            "CSV header should be present");
    require(text.find("debug-row") != std::string::npos,
            "CSV row should contain the MoorDyn status message");
    require(text.find("7,8,9") != std::string::npos,
            "CSV row should contain the returned force");

    std::filesystem::remove(csvPath);
}

void testEvaluateRequiresInitialize()
{
    auto adapter = std::make_unique<FakeMoorDynAdapter>();

    MoorDynLoadProviderConfig config{};
    config.moorDyn.inputFilePath = "Mooring/oc4_lines.txt";
    config.moorDyn.platformReferencePoint = {{0.0, 0.0, 0.54}, FrameId::global, "commonRef"};

    MoorDynLoadProvider provider(std::move(config), std::move(adapter));

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

    std::cout << "All MoorDyn load-provider tests passed.\n";
    return 0;
}
