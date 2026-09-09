#include "coupling/MoorDynCApiAdapter.hpp"

#include "coupling/Formatting.hpp"
#include "coupling/Math.hpp"

#include <array>
#include <dlfcn.h>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace oc4::coupling {

namespace {

constexpr unsigned int kExpectedCoupledDof = 6;

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
        stream << action << " failed with MoorDyn status " << status;
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
    return "libmoordyn.so";
}

ReferencePoint resolvedApplicationPoint(
    const ReferencePoint& configuredPoint,
    const PlatformState& platformState)
{
    ReferencePoint result = configuredPoint;
    result.position = platformState.positionGlobal;
    result.frame = FrameId::global;
    return result;
}

Vector3 openFoamToMoorDynPosition(
    const Vector3& openFoamPositionGlobal,
    const Vector3& openFoamToMoorDynOffsetGlobal)
{
    return openFoamPositionGlobal + openFoamToMoorDynOffsetGlobal;
}

Vector3 moorDynToOpenFoamPosition(
    const Vector3& moorDynPositionGlobal,
    const Vector3& openFoamToMoorDynOffsetGlobal)
{
    return moorDynPositionGlobal - openFoamToMoorDynOffsetGlobal;
}

}  // namespace

MoorDynApi makeRuntimeLoadedMoorDynApi(const std::string& libraryPath)
{
    const std::string resolvedPath = defaultLibraryPath(libraryPath);
    void* rawHandle = dlopen(resolvedPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (rawHandle == nullptr) {
        std::ostringstream stream;
        stream << "Failed to load MoorDyn shared library '" << resolvedPath << "'";
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

    using CreateFn = MoorDynHandle (*)(const char*);
    using GetNCoupledDofFn = int (*)(MoorDynHandle, unsigned int*);
    using SetVerbosityFn = int (*)(MoorDynHandle, int);
    using SetLogFileFn = int (*)(MoorDynHandle, const char*);
    using InitFn = int (*)(MoorDynHandle, const double*, const double*);
    using StepFn = int (*)(MoorDynHandle, const double*, const double*, double*, double*, double*);
    using CloseFn = int (*)(MoorDynHandle);
    using ExternalWaveKinInitFn = int (*)(MoorDynHandle, unsigned int*);
    using ExternalWaveKinGetNFn = int (*)(MoorDynHandle, unsigned int*);
    using ExternalWaveKinGetCoordinatesFn = int (*)(MoorDynHandle, double*);
    using ExternalWaveKinSetFn = int (*)(MoorDynHandle, const double*, const double*, double);

    MoorDynApi api{};
    api.runtimeHandle = runtimeHandle;

    const auto create = loadSymbol<CreateFn>(rawHandle, "MoorDyn_Create");
    const auto getNCoupledDof = loadSymbol<GetNCoupledDofFn>(rawHandle, "MoorDyn_NCoupledDOF");
    const auto setVerbosity = loadSymbol<SetVerbosityFn>(rawHandle, "MoorDyn_SetVerbosity");
    const auto setLogFile = loadSymbol<SetLogFileFn>(rawHandle, "MoorDyn_SetLogFile");
    const auto init = loadSymbol<InitFn>(rawHandle, "MoorDyn_Init");
    const auto initNoIc = loadSymbol<InitFn>(rawHandle, "MoorDyn_Init_NoIC");
    const auto step = loadSymbol<StepFn>(rawHandle, "MoorDyn_Step");
    const auto close = loadSymbol<CloseFn>(rawHandle, "MoorDyn_Close");
    const auto externalWaveKinInit = loadSymbol<ExternalWaveKinInitFn>(rawHandle, "MoorDyn_ExternalWaveKinInit");
    const auto externalWaveKinGetN = loadSymbol<ExternalWaveKinGetNFn>(rawHandle, "MoorDyn_ExternalWaveKinGetN");
    const auto externalWaveKinGetCoordinates =
        loadSymbol<ExternalWaveKinGetCoordinatesFn>(rawHandle, "MoorDyn_ExternalWaveKinGetCoordinates");
    const auto externalWaveKinSet = loadSymbol<ExternalWaveKinSetFn>(rawHandle, "MoorDyn_ExternalWaveKinSet");

    api.create = [create](const char* inputFilePath) {
        return create(inputFilePath);
    };
    api.getNCoupledDof = [getNCoupledDof](MoorDynHandle system, unsigned int* n) {
        return getNCoupledDof(system, n);
    };
    api.setVerbosity = [setVerbosity](MoorDynHandle system, int verbosity) {
        return setVerbosity(system, verbosity);
    };
    api.setLogFile = [setLogFile](MoorDynHandle system, const char* logFilePath) {
        return setLogFile(system, logFilePath);
    };
    api.init = [init](MoorDynHandle system, const double* x, const double* xd) {
        return init(system, x, xd);
    };
    api.initNoIc = [initNoIc](MoorDynHandle system, const double* x, const double* xd) {
        return initNoIc(system, x, xd);
    };
    api.step = [step](MoorDynHandle system, const double* x, const double* xd, double* f, double* t, double* dt) {
        return step(system, x, xd, f, t, dt);
    };
    api.close = [close](MoorDynHandle system) {
        return close(system);
    };
    api.externalWaveKinInit = [externalWaveKinInit](MoorDynHandle system, unsigned int* n) {
        return externalWaveKinInit(system, n);
    };
    api.externalWaveKinGetN = [externalWaveKinGetN](MoorDynHandle system, unsigned int* n) {
        return externalWaveKinGetN(system, n);
    };
    api.externalWaveKinGetCoordinates = [externalWaveKinGetCoordinates](MoorDynHandle system, double* coordinates) {
        return externalWaveKinGetCoordinates(system, coordinates);
    };
    api.externalWaveKinSet = [externalWaveKinSet](MoorDynHandle system, const double* velocity, const double* acceleration, double time) {
        return externalWaveKinSet(system, velocity, acceleration, time);
    };

    return api;
}

MoorDynCApiAdapter::MoorDynCApiAdapter()
    : apiExternallyProvided_(false)
{
    // Intentionally do NOT load the MoorDyn shared library here. The library is
    // loaded lazily in initialize() from MoorDynConfig::libraryPath, so merely
    // constructing the adapter never touches the filesystem and the configured
    // path (not a hard-coded default) decides which library is opened.
}

MoorDynCApiAdapter::MoorDynCApiAdapter(MoorDynApi api)
    : api_(std::move(api))
    , apiExternallyProvided_(true)
{
}

MoorDynCApiAdapter::~MoorDynCApiAdapter()
{
    close();
}

void MoorDynCApiAdapter::initialize(const MoorDynConfig& config, const PlatformState& initialState)
{
    close();

    validatePlatformState(initialState);
    require(config.platformReferencePoint.frame == FrameId::global,
            "MoorDyn platform reference point must be expressed in the global frame");
    require(!config.inputFilePath.empty(), "MoorDyn inputFilePath must not be empty");

    // Load the runtime library from the configured path unless a MoorDynApi was
    // injected (e.g. a fake in unit tests). An empty libraryPath falls back to
    // the platform default ("libmoordyn.so") inside makeRuntimeLoadedMoorDynApi.
    if (!apiExternallyProvided_) {
        api_ = makeRuntimeLoadedMoorDynApi(config.libraryPath);
    }

    require(static_cast<bool>(api_.create), "MoorDyn API create function is not available");
    require(static_cast<bool>(api_.getNCoupledDof), "MoorDyn API getNCoupledDof function is not available");
    require(static_cast<bool>(api_.init), "MoorDyn API init function is not available");
    require(static_cast<bool>(api_.initNoIc), "MoorDyn API initNoIc function is not available");
    require(static_cast<bool>(api_.step), "MoorDyn API step function is not available");
    require(static_cast<bool>(api_.close), "MoorDyn API close function is not available");

    config_ = config;
    system_ = api_.create(config_.inputFilePath.c_str());
    require(system_ != nullptr,
            "MoorDyn_Create returned null for input file '" + config_.inputFilePath + "'");

    if (api_.setVerbosity) {
        requireStatus(api_.setVerbosity(system_, config_.verbosity), "MoorDyn_SetVerbosity");
    }
    if (!config_.logFilePath.empty() && api_.setLogFile) {
        requireStatus(api_.setLogFile(system_, config_.logFilePath.c_str()), "MoorDyn_SetLogFile");
    }

    unsigned int nCoupledDof = 0;
    requireStatus(api_.getNCoupledDof(system_, &nCoupledDof), "MoorDyn_NCoupledDOF");
    require(nCoupledDof == kExpectedCoupledDof,
            "This coupling path currently supports exactly one 6-DOF coupled MoorDyn body; reported DOF="
                + std::to_string(nCoupledDof));

    const auto x = buildPositionStateVector(initialState);
    const auto xd = buildVelocityStateVector(initialState);

    if (config_.skipInitialConditionSolve) {
        requireStatus(api_.initNoIc(system_, x.data(), xd.data()), "MoorDyn_Init_NoIC");
    } else {
        requireStatus(api_.init(system_, x.data(), xd.data()), "MoorDyn_Init");
    }

    configureExternalWaveKinematics();
    initialized_ = true;
}

MoorDynResult MoorDynCApiAdapter::step(const PlatformState& platformState, double dt)
{
    require(initialized_, "MoorDyn adapter step called before initialize()");
    validatePlatformState(platformState);
    require(dt > 0.0, "MoorDyn adapter requires dt > 0");

    setExternalWaveKinematics(platformState.time);

    const auto x = buildPositionStateVector(platformState);
    const auto xd = buildVelocityStateVector(platformState);
    std::array<double, 6> load{};
    double time = platformState.time;
    double stepSize = dt;

    requireStatus(api_.step(system_, x.data(), xd.data(), load.data(), &time, &stepSize), "MoorDyn_Step");

    MoorDynResult result{};
    result.resultantLoad.force = {load[0], load[1], load[2]};
    result.resultantLoad.moment = {load[3], load[4], load[5]};
    result.resultantLoad.applicationPoint = resolvedApplicationPoint(config_.platformReferencePoint, platformState);
    result.resultantLoad.expressedInFrame = FrameId::global;
    result.statusCode = 0;
    result.statusMessage = "MoorDyn step completed";
    return result;
}

void MoorDynCApiAdapter::close()
{
    if (system_ != nullptr && api_.close) {
        api_.close(system_);
    }

    system_ = nullptr;
    initialized_ = false;
    externalWaveSamplePointsGlobal_.clear();
}

std::array<double, 6> MoorDynCApiAdapter::buildPositionStateVector(const PlatformState& platformState) const
{
    const Vector3 moorDynPositionGlobal =
        openFoamToMoorDynPosition(platformState.positionGlobal, config_.openFoamToMoorDynOffsetGlobal);

    return {
        moorDynPositionGlobal.x,
        moorDynPositionGlobal.y,
        moorDynPositionGlobal.z,
        platformState.eulerAnglesRad.x,
        platformState.eulerAnglesRad.y,
        platformState.eulerAnglesRad.z,
    };
}

std::array<double, 6> MoorDynCApiAdapter::buildVelocityStateVector(const PlatformState& platformState)
{
    // MoorDyn-C consumes the rotational part of the coupled-body velocity as the
    // body ANGULAR VELOCITY (omega): it forms the quaternion rate qDot = 0.5*w*q
    // and the fairlead velocities v = v_t + w x r (see MoorDyn Body.cpp). It is
    // NOT the XYZ Euler-angle time derivative. The position vector still carries
    // XYZ Euler angles (consistent with MoorDyn's Euler2Quat = Rx*Ry*Rz). For the
    // small platform rotations in OC4 the two coincide to high order, but passing
    // omega is the physically correct input for the line-drag/damping path.
    return {
        platformState.velocityGlobal.x,
        platformState.velocityGlobal.y,
        platformState.velocityGlobal.z,
        platformState.angularVelocityGlobal.x,
        platformState.angularVelocityGlobal.y,
        platformState.angularVelocityGlobal.z,
    };
}

void MoorDynCApiAdapter::validatePlatformState(const PlatformState& platformState)
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

void MoorDynCApiAdapter::configureExternalWaveKinematics()
{
    externalWaveSamplePointsGlobal_.clear();
    if (!config_.externalWaveKinematicsProvider) {
        return;
    }

    require(static_cast<bool>(api_.externalWaveKinInit),
            "MoorDyn externalWaveKinInit function is not available");
    require(static_cast<bool>(api_.externalWaveKinGetCoordinates),
            "MoorDyn externalWaveKinGetCoordinates function is not available");

    unsigned int nWavePoints = 0;
    requireStatus(api_.externalWaveKinInit(system_, &nWavePoints), "MoorDyn_ExternalWaveKinInit");

    if (nWavePoints == 0U) {
        return;
    }

    std::vector<double> coordinates(3U * nWavePoints, 0.0);
    requireStatus(api_.externalWaveKinGetCoordinates(system_, coordinates.data()), "MoorDyn_ExternalWaveKinGetCoordinates");

    externalWaveSamplePointsGlobal_.reserve(nWavePoints);
    for (unsigned int i = 0; i < nWavePoints; ++i) {
        const Vector3 moorDynPointGlobal{
            coordinates[3U * i + 0U],
            coordinates[3U * i + 1U],
            coordinates[3U * i + 2U],
        };
        externalWaveSamplePointsGlobal_.push_back(
            moorDynToOpenFoamPosition(moorDynPointGlobal, config_.openFoamToMoorDynOffsetGlobal));
    }
}

void MoorDynCApiAdapter::setExternalWaveKinematics(double time)
{
    if (!config_.externalWaveKinematicsProvider) {
        return;
    }
    if (externalWaveSamplePointsGlobal_.empty()) {
        return;
    }

    require(static_cast<bool>(api_.externalWaveKinSet),
            "MoorDyn externalWaveKinSet function is not available");

    std::vector<Vector3> velocityGlobal;
    std::vector<Vector3> accelerationGlobal;
    config_.externalWaveKinematicsProvider(
        time,
        externalWaveSamplePointsGlobal_,
        velocityGlobal,
        accelerationGlobal);

    require(velocityGlobal.size() == externalWaveSamplePointsGlobal_.size(),
            "External wave kinematics provider returned a velocity vector with the wrong size");
    require(accelerationGlobal.size() == externalWaveSamplePointsGlobal_.size(),
            "External wave kinematics provider returned an acceleration vector with the wrong size");

    std::vector<double> velocityFlat(3U * velocityGlobal.size(), 0.0);
    std::vector<double> accelerationFlat(3U * accelerationGlobal.size(), 0.0);

    for (std::size_t i = 0; i < velocityGlobal.size(); ++i) {
        velocityFlat[3U * i + 0U] = velocityGlobal[i].x;
        velocityFlat[3U * i + 1U] = velocityGlobal[i].y;
        velocityFlat[3U * i + 2U] = velocityGlobal[i].z;
        accelerationFlat[3U * i + 0U] = accelerationGlobal[i].x;
        accelerationFlat[3U * i + 1U] = accelerationGlobal[i].y;
        accelerationFlat[3U * i + 2U] = accelerationGlobal[i].z;
    }

    requireStatus(api_.externalWaveKinSet(system_, velocityFlat.data(), accelerationFlat.data(), time),
                  "MoorDyn_ExternalWaveKinSet");
}

}  // namespace oc4::coupling
