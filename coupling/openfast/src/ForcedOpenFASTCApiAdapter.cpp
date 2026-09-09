#include "coupling/ForcedOpenFASTCApiAdapter.hpp"

#include "coupling/Formatting.hpp"
#include "coupling/Math.hpp"

#include <array>
#include <dlfcn.h>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace oc4::coupling {

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireStatus(int status, const std::string& action)
{
    if (status != 0) {
        std::ostringstream stream;
        stream << action << " failed with OpenFAST status " << status;
        throw std::runtime_error(stream.str());
    }
}

template <typename FunctionPointer>
FunctionPointer loadSymbol(void* libraryHandle, const char* symbolName)
{
    dlerror();
    auto* symbol = dlsym(libraryHandle, symbolName);
    const char* error = dlerror();
    if (error != nullptr || symbol == nullptr) {
        std::ostringstream stream;
        stream << "Failed to load symbol '" << symbolName << "'";
        if (error != nullptr) {
            stream << ": " << error;
        }
        throw std::runtime_error(stream.str());
    }

    return reinterpret_cast<FunctionPointer>(symbol);
}

std::string defaultLibraryPath(const std::string& libraryPath)
{
    if (!libraryPath.empty()) {
        return libraryPath;
    }
    return "libforcedopenfast.so";
}

Vector3 openFoamToOpenFastPosition(
    const Vector3& openFoamPositionGlobal,
    const Vector3& openFoamToOpenFastOffsetGlobal)
{
    return openFoamPositionGlobal + openFoamToOpenFastOffsetGlobal;
}

Vector3 openFastToOpenFoamPosition(
    const Vector3& openFastPositionGlobal,
    const Vector3& openFoamToOpenFastOffsetGlobal)
{
    return openFastPositionGlobal - openFoamToOpenFastOffsetGlobal;
}

}  // namespace

OpenFASTApi makeRuntimeLoadedOpenFASTApi(const std::string& libraryPath)
{
    const std::string resolvedPath = defaultLibraryPath(libraryPath);
    void* rawHandle = dlopen(resolvedPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (rawHandle == nullptr) {
        std::ostringstream stream;
        stream << "Failed to load OpenFAST shared library '" << resolvedPath << "'";
        const char* error = dlerror();
        if (error != nullptr) {
            stream << ": " << error;
        }
        throw std::runtime_error(stream.str());
    }

    auto runtimeHandle = std::shared_ptr<void>(rawHandle, [](void* handle) {
        if (handle != nullptr) {
            dlclose(handle);
        }
    });

    using CreateFn = OpenFASTHandle (*)(const char*);
    using InitializeFn = int (*)(OpenFASTHandle, const double*, const double*, const double*);
    using StepFn = int (*)(OpenFASTHandle, const double*, const double*, const double*, double*, double*);
    using GetTowerBaseLoadFn = int (*)(OpenFASTHandle, double*, double*, double*);
    using GetScalarFn = int (*)(OpenFASTHandle, double*);
    using GetVector3Fn = int (*)(OpenFASTHandle, double*);
    using GetBladePitchFn = int (*)(OpenFASTHandle, double*);
    using GetControllerStatusFn = int (*)(OpenFASTHandle, int*);
    using CloseFn = int (*)(OpenFASTHandle);

    OpenFASTApi api{};
    api.runtimeHandle = runtimeHandle;

    const auto create = loadSymbol<CreateFn>(rawHandle, "ForcedOpenFAST_Create");
    const auto initialize = loadSymbol<InitializeFn>(rawHandle, "ForcedOpenFAST_Initialize");
    const auto step = loadSymbol<StepFn>(rawHandle, "ForcedOpenFAST_Step");
    const auto getTowerBaseLoad = loadSymbol<GetTowerBaseLoadFn>(rawHandle, "ForcedOpenFAST_GetTowerBaseLoad");
    const auto getRotorSpeed = loadSymbol<GetScalarFn>(rawHandle, "ForcedOpenFAST_GetRotorSpeed");
    const auto getGeneratorPower = loadSymbol<GetScalarFn>(rawHandle, "ForcedOpenFAST_GetGeneratorPower");
    const auto getBladePitch = loadSymbol<GetBladePitchFn>(rawHandle, "ForcedOpenFAST_GetBladePitch");
    const auto getTowerTopDisplacement =
        loadSymbol<GetVector3Fn>(rawHandle, "ForcedOpenFAST_GetTowerTopDisplacement");
    const auto getControllerStatus =
        loadSymbol<GetControllerStatusFn>(rawHandle, "ForcedOpenFAST_GetControllerStatus");
    const auto close = loadSymbol<CloseFn>(rawHandle, "ForcedOpenFAST_Close");

    api.create = [create](const char* casePath) {
        return create(casePath);
    };
    api.initialize = [initialize](OpenFASTHandle handle, const double* displacement, const double* velocity, const double* acceleration) {
        return initialize(handle, displacement, velocity, acceleration);
    };
    api.step = [step](OpenFASTHandle handle, const double* displacement, const double* velocity, const double* acceleration, double* time, double* dt) {
        return step(handle, displacement, velocity, acceleration, time, dt);
    };
    api.getTowerBaseLoad = [getTowerBaseLoad](OpenFASTHandle handle, double* applicationPoint, double* force, double* moment) {
        return getTowerBaseLoad(handle, applicationPoint, force, moment);
    };
    api.getRotorSpeed = [getRotorSpeed](OpenFASTHandle handle, double* rotorSpeedRadPerSec) {
        return getRotorSpeed(handle, rotorSpeedRadPerSec);
    };
    api.getGeneratorPower = [getGeneratorPower](OpenFASTHandle handle, double* generatorPowerW) {
        return getGeneratorPower(handle, generatorPowerW);
    };
    api.getBladePitch = [getBladePitch](OpenFASTHandle handle, double* bladePitchRad) {
        return getBladePitch(handle, bladePitchRad);
    };
    api.getTowerTopDisplacement = [getTowerTopDisplacement](OpenFASTHandle handle, double* towerTopDisplacementGlobal) {
        return getTowerTopDisplacement(handle, towerTopDisplacementGlobal);
    };
    api.getControllerStatus = [getControllerStatus](OpenFASTHandle handle, int* controllerOk) {
        return getControllerStatus(handle, controllerOk);
    };
    api.close = [close](OpenFASTHandle handle) {
        return close(handle);
    };

    return api;
}

ForcedOpenFASTCApiAdapter::ForcedOpenFASTCApiAdapter()
{
}

ForcedOpenFASTCApiAdapter::ForcedOpenFASTCApiAdapter(OpenFASTApi api)
    : api_(std::move(api))
{
}

ForcedOpenFASTCApiAdapter::~ForcedOpenFASTCApiAdapter()
{
    close();
}

void ForcedOpenFASTCApiAdapter::initialize(const OpenFASTConfig& config, const PlatformState& initialState)
{
    close();

    validatePlatformState(initialState);
    require(config.platformReferencePoint.frame == FrameId::global,
            "OpenFAST platform reference point must be expressed in the global frame");
    require(config.turbineInterfacePoint.frame == FrameId::global,
            "OpenFAST turbine interface point must be expressed in the global frame");
    require(!config.casePath.empty(), "OpenFAST casePath must not be empty");

    config_ = config;
    if (!api_.create) {
        api_ = makeRuntimeLoadedOpenFASTApi(config_.libraryPath);
    }

    require(static_cast<bool>(api_.create), "OpenFAST API create function is not available");
    require(static_cast<bool>(api_.initialize), "OpenFAST API initialize function is not available");
    require(static_cast<bool>(api_.step), "OpenFAST API step function is not available");
    require(static_cast<bool>(api_.getTowerBaseLoad), "OpenFAST API getTowerBaseLoad function is not available");
    require(static_cast<bool>(api_.close), "OpenFAST API close function is not available");

    handle_ = api_.create(config_.casePath.c_str());
    require(handle_ != nullptr,
            "ForcedOpenFAST_Create returned null for case path '" + config_.casePath + "'");

    const auto displacement = buildDisplacementStateVector(initialState);
    const auto velocity = buildVelocityStateVector(initialState);
    const auto acceleration = buildAccelerationStateVector(initialState);

    requireStatus(api_.initialize(handle_, displacement.data(), velocity.data(), acceleration.data()),
                  "ForcedOpenFAST_Initialize");

    initialized_ = true;
}

OpenFASTResult ForcedOpenFASTCApiAdapter::step(const PlatformState& platformState, double dt)
{
    require(initialized_, "ForcedOpenFAST adapter step called before initialize()");
    validatePlatformState(platformState);
    require(dt > 0.0, "ForcedOpenFAST adapter requires dt > 0");

    const auto displacement = buildDisplacementStateVector(platformState);
    const auto velocity = buildVelocityStateVector(platformState);
    const auto acceleration = buildAccelerationStateVector(platformState);
    double time = platformState.time;
    double stepSize = dt;

    requireStatus(api_.step(handle_, displacement.data(), velocity.data(), acceleration.data(), &time, &stepSize),
                  "ForcedOpenFAST_Step");

    OpenFASTResult result{};
    std::array<double, 3> applicationPoint{};
    std::array<double, 3> force{};
    std::array<double, 3> moment{};
    requireStatus(api_.getTowerBaseLoad(handle_, applicationPoint.data(), force.data(), moment.data()),
                  "ForcedOpenFAST_GetTowerBaseLoad");

    const Vector3 applicationPointGlobal = openFastToOpenFoamPosition(
        {applicationPoint[0], applicationPoint[1], applicationPoint[2]},
        config_.openFoamToOpenFastOffsetGlobal);

    result.resultantLoad.force = {force[0], force[1], force[2]};
    result.resultantLoad.moment = {moment[0], moment[1], moment[2]};
    result.resultantLoad.applicationPoint = {
        applicationPointGlobal,
        FrameId::global,
        config_.turbineInterfacePoint.name
    };
    result.resultantLoad.expressedInFrame = FrameId::global;

    if (api_.getRotorSpeed) {
        requireStatus(api_.getRotorSpeed(handle_, &result.rotorSpeedRadPerSec),
                      "ForcedOpenFAST_GetRotorSpeed");
    }
    if (api_.getGeneratorPower) {
        requireStatus(api_.getGeneratorPower(handle_, &result.generatorPowerW),
                      "ForcedOpenFAST_GetGeneratorPower");
    }
    if (api_.getBladePitch) {
        requireStatus(api_.getBladePitch(handle_, result.bladePitchRad.data()),
                      "ForcedOpenFAST_GetBladePitch");
    }
    if (api_.getTowerTopDisplacement) {
        std::array<double, 3> towerTop{};
        requireStatus(api_.getTowerTopDisplacement(handle_, towerTop.data()),
                      "ForcedOpenFAST_GetTowerTopDisplacement");
        result.towerTopDisplacementGlobal = {towerTop[0], towerTop[1], towerTop[2]};
    }
    if (api_.getControllerStatus) {
        int controllerOk = 1;
        requireStatus(api_.getControllerStatus(handle_, &controllerOk),
                      "ForcedOpenFAST_GetControllerStatus");
        result.controllerOk = (controllerOk != 0);
    }

    result.statusCode = 0;
    result.statusMessage = "ForcedOpenFAST step completed";
    return result;
}

void ForcedOpenFASTCApiAdapter::close()
{
    if (handle_ != nullptr && api_.close) {
        api_.close(handle_);
    }

    handle_ = nullptr;
    initialized_ = false;
}

std::array<double, 6> ForcedOpenFASTCApiAdapter::buildDisplacementStateVector(const PlatformState& platformState) const
{
    const Vector3 openFastPositionGlobal =
        openFoamToOpenFastPosition(platformState.positionGlobal, config_.openFoamToOpenFastOffsetGlobal);

    return {
        openFastPositionGlobal.x,
        openFastPositionGlobal.y,
        openFastPositionGlobal.z,
        platformState.eulerAnglesRad.x,
        platformState.eulerAnglesRad.y,
        platformState.eulerAnglesRad.z,
    };
}

std::array<double, 6> ForcedOpenFASTCApiAdapter::buildVelocityStateVector(const PlatformState& platformState)
{
    return {
        platformState.velocityGlobal.x,
        platformState.velocityGlobal.y,
        platformState.velocityGlobal.z,
        platformState.angularVelocityGlobal.x,
        platformState.angularVelocityGlobal.y,
        platformState.angularVelocityGlobal.z,
    };
}

std::array<double, 6> ForcedOpenFASTCApiAdapter::buildAccelerationStateVector(const PlatformState& platformState)
{
    return {
        platformState.accelerationGlobal.x,
        platformState.accelerationGlobal.y,
        platformState.accelerationGlobal.z,
        platformState.angularAccelerationGlobal.x,
        platformState.angularAccelerationGlobal.y,
        platformState.angularAccelerationGlobal.z,
    };
}

void ForcedOpenFASTCApiAdapter::validatePlatformState(const PlatformState& platformState)
{
    require(platformState.referencePoint.frame == FrameId::global,
            "Platform reference point must be expressed in the global frame");
    require(isProperRotationMatrix(platformState.orientationGlobalFromBody),
            "Platform orientationGlobalFromBody is not a proper rotation matrix");

    if (!nearlyEqual(platformState.referencePoint.position, platformState.positionGlobal, 1e-9)) {
        std::ostringstream stream;
        stream << "Platform reference point position and positionGlobal must match. referencePoint="
               << toString(platformState.referencePoint.position)
               << " positionGlobal=" << toString(platformState.positionGlobal);
        throw std::runtime_error(stream.str());
    }
}

}  // namespace oc4::coupling
