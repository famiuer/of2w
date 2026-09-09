#include "coupling/ForcedOpenFASTCApi.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>

namespace {

constexpr int kInterfaceStringLength = 1025;

typedef struct ExtLdDX_InputType {
    void* object;
    double* twrDef;
    int twrDef_Len;
    double* bldDef;
    int bldDef_Len;
    double* hubDef;
    int hubDef_Len;
    double* nacDef;
    int nacDef_Len;
    double* bldRootDef;
    int bldRootDef_Len;
    double* bldPitch;
    int bldPitch_Len;
} ExtLdDX_InputType_t;

typedef struct ExtLdDX_ParameterType {
    void* object;
    int* nBlades;
    int nBlades_Len;
    int* nBladeNodes;
    int nBladeNodes_Len;
    int* nTowerNodes;
    int nTowerNodes_Len;
    double* twrRefPos;
    int twrRefPos_Len;
    double* bldRefPos;
    int bldRefPos_Len;
    double* hubRefPos;
    int hubRefPos_Len;
    double* nacRefPos;
    int nacRefPos_Len;
    double* bldRootRefPos;
    int bldRootRefPos_Len;
    double* bldChord;
    int bldChord_Len;
    double* bldRloc;
    int bldRloc_Len;
    double* twrDia;
    int twrDia_Len;
    double* twrHloc;
    int twrHloc_Len;
} ExtLdDX_ParameterType_t;

typedef struct ExtLdDX_OutputType {
    void* object;
    double* twrLd;
    int twrLd_Len;
    double* bldLd;
    int bldLd_Len;
} ExtLdDX_OutputType_t;

enum class RuntimeBackend {
    stub,
    openfastLibrary
};

using FastAllocateTurbinesFn = void (*)(int*, int*, char*);
using FastDeallocateTurbinesFn = void (*)(int*, char*);
using FastExtLoadsInitFn = void (*)(
    int*,
    double*,
    const char*,
    int*,
    char*,
    float*,
    int*,
    double*,
    double*,
    int*,
    double*,
    double*,
    ExtLdDX_InputType_t*,
    ExtLdDX_ParameterType_t*,
    ExtLdDX_OutputType_t*,
    int*,
    char*);
using FastCfdStepFn = void (*)(int*, int*, char*);
using FastEndFn = void (*)(int*, bool*);
using OpenFastBridgeSetMotionFn =
    void (*)(int*, const double*, const double*, const double*, int*, char*);
using OpenFastBridgeGetLoadFn =
    void (*)(int*, double*, double*, double*, int*, char*);
using OpenFastBridgeGetDiagnosticsFn =
    void (*)(int*, double*, double*, double*, double*, int*, int*, char*);

struct OpenFastLibraryApi {
    FastAllocateTurbinesFn allocateTurbines{nullptr};
    FastDeallocateTurbinesFn deallocateTurbines{nullptr};
    FastExtLoadsInitFn extLoadsInit{nullptr};
    FastCfdStepFn cfdSolution0{nullptr};
    FastCfdStepFn cfdPrework{nullptr};
    FastCfdStepFn cfdUpdateStates{nullptr};
    FastCfdStepFn cfdAdvanceToNextTimeStep{nullptr};
    FastCfdStepFn cfdWriteOutput{nullptr};
    FastEndFn end{nullptr};
    OpenFastBridgeSetMotionFn bridgeSetPlatformMotion{nullptr};
    OpenFastBridgeGetLoadFn bridgeGetPlatformReactionLoad{nullptr};
    OpenFastBridgeGetDiagnosticsFn bridgeGetDiagnostics{nullptr};
};

struct ForcedOpenFASTInstance {
    std::string casePath{};
    std::string manifestPath{};
    RuntimeBackend backend{RuntimeBackend::stub};
    std::string backendName{"stub"};
    std::string openfastInput{};
    std::string outRoot{};
    std::string openfastLibraryPath{};
    std::string simStart{"init"};
    double dtOpenFAST{0.01};
    double tMax{1.0};
    int substepsPerCouplingStep{1};
    std::array<double, 3> platformReferencePoint{0.0, 0.0, 0.54};
    std::array<double, 3> towerBasePoint{0.0, 0.0, 10.0};
    std::array<double, 3> openFoamToOpenFastOffset{0.0, 0.0, -15.0};
    std::array<double, 6> displacement{};
    std::array<double, 6> velocity{};
    std::array<double, 6> acceleration{};
    std::array<double, 6> previousDisplacement{};
    std::array<double, 6> previousVelocity{};
    std::array<double, 6> previousAcceleration{};
    std::array<double, 3> applicationPoint{0.0, 0.0, 10.0};
    std::array<double, 3> force{};
    std::array<double, 3> moment{};
    std::array<double, 3> towerTopDisplacement{};
    std::array<double, 3> bladePitch{};
    double rotorSpeedRadPerSec{0.0};
    double generatorPowerW{0.0};
    int controllerOk{1};
    bool initialized{false};
    bool backendValidated{false};
    bool bridgeAvailable{false};
    void* backendLibraryHandle{nullptr};
    std::string backendLibraryResolvedPath{};
    OpenFastLibraryApi openfastApi{};
    ExtLdDX_InputType_t extLoadsInput{};
    ExtLdDX_ParameterType_t extLoadsParameters{};
    ExtLdDX_OutputType_t extLoadsOutput{};
    double backendDt{0.0};
    int numBlades{0};
    int abortErrorLevel{0};
    std::string outFileRoot{};
    std::string lastError{};
};

constexpr const char* kVersionString = "forced_openfast_runtime/0.3";

std::string trim(const std::string& text)
{
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(start, end - start);
}

std::string stripInlineComment(const std::string& text)
{
    const std::size_t hash = text.find('#');
    if (hash == std::string::npos) {
        return text;
    }
    return text.substr(0, hash);
}

bool parseQuotedString(const std::string& value, std::string& output)
{
    const std::string trimmed = trim(value);
    if (trimmed.size() < 2 || trimmed.front() != '"' || trimmed.back() != '"') {
        return false;
    }

    output = trimmed.substr(1, trimmed.size() - 2);
    return true;
}

bool parseArray3(const std::string& value, std::array<double, 3>& output)
{
    const std::string trimmed = trim(value);
    if (trimmed.size() < 5 || trimmed.front() != '[' || trimmed.back() != ']') {
        return false;
    }

    std::string contents = trimmed.substr(1, trimmed.size() - 2);
    std::replace(contents.begin(), contents.end(), ',', ' ');
    std::istringstream stream(contents);
    return static_cast<bool>(stream >> output[0] >> output[1] >> output[2]);
}

bool parseDouble(const std::string& value, double& output)
{
    std::istringstream stream(trim(value));
    return static_cast<bool>(stream >> output);
}

bool parseInt(const std::string& value, int& output)
{
    std::istringstream stream(trim(value));
    return static_cast<bool>(stream >> output);
}

bool parseManifest(
    const std::filesystem::path& manifestPath,
    ForcedOpenFASTInstance& instance,
    std::string& errorMessage)
{
    std::ifstream input(manifestPath);
    if (!input.is_open()) {
        errorMessage = "Unable to open manifest: " + manifestPath.string();
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        const std::string cleaned = trim(stripInlineComment(line));
        if (cleaned.empty() || cleaned.front() == '[') {
            continue;
        }

        const std::size_t equal = cleaned.find('=');
        if (equal == std::string::npos) {
            continue;
        }

        const std::string key = trim(cleaned.substr(0, equal));
        const std::string value = trim(cleaned.substr(equal + 1));

        if (key == "backend") {
            if (!parseQuotedString(value, instance.backendName)) {
                errorMessage = "Invalid backend entry in manifest";
                return false;
            }
        } else if (key == "openfast_input") {
            if (!parseQuotedString(value, instance.openfastInput)) {
                errorMessage = "Invalid openfast_input entry in manifest";
                return false;
            }
        } else if (key == "out_root") {
            if (!parseQuotedString(value, instance.outRoot)) {
                errorMessage = "Invalid out_root entry in manifest";
                return false;
            }
        } else if (key == "openfast_library_path") {
            if (!parseQuotedString(value, instance.openfastLibraryPath)) {
                errorMessage = "Invalid openfast_library_path entry in manifest";
                return false;
            }
        } else if (key == "sim_start") {
            if (!parseQuotedString(value, instance.simStart)) {
                errorMessage = "Invalid sim_start entry in manifest";
                return false;
            }
        } else if (key == "dt_openfast") {
            if (!parseDouble(value, instance.dtOpenFAST)) {
                errorMessage = "Invalid dt_openfast entry in manifest";
                return false;
            }
        } else if (key == "t_max") {
            if (!parseDouble(value, instance.tMax)) {
                errorMessage = "Invalid t_max entry in manifest";
                return false;
            }
        } else if (key == "substeps_per_coupling_step") {
            if (!parseInt(value, instance.substepsPerCouplingStep)) {
                errorMessage = "Invalid substeps_per_coupling_step entry in manifest";
                return false;
            }
        } else if (key == "platform_reference_point") {
            if (!parseArray3(value, instance.platformReferencePoint)) {
                errorMessage = "Invalid platform_reference_point entry in manifest";
                return false;
            }
        } else if (key == "tower_base_point") {
            if (!parseArray3(value, instance.towerBasePoint)) {
                errorMessage = "Invalid tower_base_point entry in manifest";
                return false;
            }
        } else if (key == "openfoam_to_openfast_offset") {
            if (!parseArray3(value, instance.openFoamToOpenFastOffset)) {
                errorMessage = "Invalid openfoam_to_openfast_offset entry in manifest";
                return false;
            }
        }
    }

    if (instance.backendName == "stub") {
        instance.backend = RuntimeBackend::stub;
    } else if (instance.backendName == "openfast_library") {
        instance.backend = RuntimeBackend::openfastLibrary;
    } else {
        errorMessage = "Unsupported backend '" + instance.backendName + "'";
        return false;
    }

    if (instance.substepsPerCouplingStep < 1) {
        errorMessage = "substeps_per_coupling_step must be >= 1";
        return false;
    }

    return true;
}

ForcedOpenFASTInstance* fromHandle(ForcedOpenFASTHandle handle)
{
    return reinterpret_cast<ForcedOpenFASTInstance*>(handle);
}

void setError(ForcedOpenFASTInstance* instance, const std::string& message)
{
    if (instance != nullptr) {
        instance->lastError = message;
    }
}

std::filesystem::path resolveCaseRelativePath(
    const std::filesystem::path& casePath,
    const std::string& configuredPath)
{
    const std::filesystem::path rawPath(configuredPath);
    if (rawPath.is_absolute()) {
        return rawPath;
    }
    return casePath / rawPath;
}

bool validateCommonCaseFiles(ForcedOpenFASTInstance& instance, std::string& errorMessage)
{
    if (instance.openfastInput.empty()) {
        errorMessage = "Manifest must define openfast_input";
        return false;
    }

    const std::filesystem::path openfastInputPath =
        std::filesystem::path(instance.casePath) / instance.openfastInput;
    if (!std::filesystem::exists(openfastInputPath)) {
        errorMessage = "Referenced openfast_input does not exist: " + openfastInputPath.string();
        return false;
    }

    if (!instance.outRoot.empty()) {
        std::error_code errorCode;
        std::filesystem::create_directories(std::filesystem::path(instance.casePath) / instance.outRoot,
                                            errorCode);
        if (errorCode) {
            errorMessage = "Unable to create out_root directory: " + errorCode.message();
            return false;
        }
    }

    return true;
}

std::string cStringToStdString(const char* chars)
{
    if (chars == nullptr) {
        return {};
    }

    const std::size_t maxLen = static_cast<std::size_t>(kInterfaceStringLength - 1);
    std::size_t len = 0;
    while (len < maxLen && chars[len] != '\0') {
        ++len;
    }
    return std::string(chars, len);
}

template <typename FunctionPointer>
FunctionPointer loadSymbol(void* libraryHandle, const char* symbolName, std::string& errorMessage)
{
    dlerror();
    void* symbol = dlsym(libraryHandle, symbolName);
    const char* error = dlerror();
    if (symbol == nullptr || error != nullptr) {
        std::ostringstream stream;
        stream << "Missing required OpenFAST library symbol '" << symbolName << "'";
        if (error != nullptr) {
            stream << ": " << error;
        }
        errorMessage = stream.str();
        return nullptr;
    }
    return reinterpret_cast<FunctionPointer>(symbol);
}

template <typename FunctionPointer>
FunctionPointer loadOptionalSymbol(void* libraryHandle, const char* symbolName)
{
    dlerror();
    void* symbol = dlsym(libraryHandle, symbolName);
    const char* error = dlerror();
    if (symbol == nullptr || error != nullptr) {
        return nullptr;
    }
    return reinterpret_cast<FunctionPointer>(symbol);
}

bool validateOpenFASTLibraryBackend(ForcedOpenFASTInstance& instance, std::string& errorMessage)
{
    if (instance.openfastLibraryPath.empty()) {
        errorMessage =
            "Manifest must define openfast_library_path when backend = \"openfast_library\"";
        return false;
    }

    const std::filesystem::path resolvedLibraryPath =
        resolveCaseRelativePath(instance.casePath, instance.openfastLibraryPath);
    if (!std::filesystem::exists(resolvedLibraryPath)) {
        errorMessage =
            "Referenced openfast_library_path does not exist: " + resolvedLibraryPath.string();
        return false;
    }

    void* libraryHandle = dlopen(resolvedLibraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (libraryHandle == nullptr) {
        std::ostringstream stream;
        stream << "Failed to load OpenFAST library '" << resolvedLibraryPath.string() << "'";
        const char* error = dlerror();
        if (error != nullptr) {
            stream << ": " << error;
        }
        errorMessage = stream.str();
        return false;
    }

    OpenFastLibraryApi api{};
    api.allocateTurbines =
        loadSymbol<FastAllocateTurbinesFn>(libraryHandle, "FAST_AllocateTurbines", errorMessage);
    api.deallocateTurbines = loadSymbol<FastDeallocateTurbinesFn>(
        libraryHandle, "FAST_DeallocateTurbines", errorMessage);
    api.extLoadsInit =
        loadSymbol<FastExtLoadsInitFn>(libraryHandle, "FAST_ExtLoads_Init", errorMessage);
    api.cfdSolution0 =
        loadSymbol<FastCfdStepFn>(libraryHandle, "FAST_CFD_Solution0", errorMessage);
    api.cfdPrework =
        loadSymbol<FastCfdStepFn>(libraryHandle, "FAST_CFD_Prework", errorMessage);
    api.cfdUpdateStates =
        loadSymbol<FastCfdStepFn>(libraryHandle, "FAST_CFD_UpdateStates", errorMessage);
    api.cfdAdvanceToNextTimeStep = loadSymbol<FastCfdStepFn>(
        libraryHandle, "FAST_CFD_AdvanceToNextTimeStep", errorMessage);
    api.cfdWriteOutput =
        loadSymbol<FastCfdStepFn>(libraryHandle, "FAST_CFD_WriteOutput", errorMessage);
    api.end = loadSymbol<FastEndFn>(libraryHandle, "FAST_End", errorMessage);

    if (!api.allocateTurbines || !api.deallocateTurbines || !api.extLoadsInit || !api.cfdSolution0 ||
        !api.cfdPrework || !api.cfdUpdateStates || !api.cfdAdvanceToNextTimeStep ||
        !api.cfdWriteOutput || !api.end) {
        dlclose(libraryHandle);
        return false;
    }

    api.bridgeSetPlatformMotion = loadOptionalSymbol<OpenFastBridgeSetMotionFn>(
        libraryHandle, "FAST_OC4_Platform_SetMotion");
    api.bridgeGetPlatformReactionLoad = loadOptionalSymbol<OpenFastBridgeGetLoadFn>(
        libraryHandle, "FAST_OC4_Platform_GetReactionLoad");
    api.bridgeGetDiagnostics = loadOptionalSymbol<OpenFastBridgeGetDiagnosticsFn>(
        libraryHandle, "FAST_OC4_Platform_GetDiagnostics");

    const bool hasSetMotion = api.bridgeSetPlatformMotion != nullptr;
    const bool hasGetLoad = api.bridgeGetPlatformReactionLoad != nullptr;
    if (hasSetMotion != hasGetLoad) {
        errorMessage =
            "OpenFAST bridge library must provide both FAST_OC4_Platform_SetMotion and "
            "FAST_OC4_Platform_GetReactionLoad, or neither";
        dlclose(libraryHandle);
        return false;
    }

    instance.backendLibraryHandle = libraryHandle;
    instance.backendLibraryResolvedPath = resolvedLibraryPath.string();
    instance.openfastApi = api;
    instance.bridgeAvailable = hasSetMotion && hasGetLoad;
    instance.backendValidated = true;
    return true;
}

void closeBackendLibrary(ForcedOpenFASTInstance& instance)
{
    if (instance.backendLibraryHandle != nullptr) {
        dlclose(instance.backendLibraryHandle);
        instance.backendLibraryHandle = nullptr;
    }
    instance.backendValidated = false;
    instance.bridgeAvailable = false;
    instance.backendLibraryResolvedPath.clear();
    instance.openfastApi = {};
}

int validateMotionArrays(
    ForcedOpenFASTInstance* instance,
    const double* displacement,
    const double* velocity,
    const double* acceleration)
{
    if (instance == nullptr) {
        return FORCED_OPENFAST_INVALID_ARGUMENT;
    }
    if (displacement == nullptr || velocity == nullptr || acceleration == nullptr) {
        setError(instance, "ForcedOpenFAST motion arrays must not be null");
        return FORCED_OPENFAST_INVALID_ARGUMENT;
    }
    return FORCED_OPENFAST_SUCCESS;
}

void copyArray6(const double* input, std::array<double, 6>& output)
{
    for (int i = 0; i < 6; ++i) {
        output[static_cast<std::size_t>(i)] = input[i];
    }
}

std::array<double, 6> interpolateStateArray(
    const std::array<double, 6>& previousValues,
    const std::array<double, 6>& currentValues,
    double fraction)
{
    std::array<double, 6> interpolated{};
    for (int i = 0; i < 6; ++i) {
        interpolated[static_cast<std::size_t>(i)] =
            previousValues[static_cast<std::size_t>(i)] +
            fraction * (currentValues[static_cast<std::size_t>(i)] -
                        previousValues[static_cast<std::size_t>(i)]);
    }
    return interpolated;
}

void evaluateStubLoads(ForcedOpenFASTInstance& instance)
{
    const double surge = instance.displacement[0];
    const double sway = instance.displacement[1];
    const double heave = instance.displacement[2];
    const double roll = instance.displacement[3];
    const double pitch = instance.displacement[4];
    const double yaw = instance.displacement[5];

    const double vx = instance.velocity[0];
    const double vy = instance.velocity[1];
    const double vz = instance.velocity[2];
    const double wx = instance.velocity[3];
    const double wy = instance.velocity[4];
    const double wz = instance.velocity[5];

    instance.force = {
        -2.5e4 * surge - 2.5e3 * vx,
        -2.5e4 * sway - 2.5e3 * vy,
        -1.5e4 * heave - 1.5e3 * vz
    };
    instance.moment = {
        -5.0e6 * roll - 5.0e5 * wx,
        -5.0e6 * pitch - 5.0e5 * wy,
        -2.5e6 * yaw - 2.5e5 * wz
    };

    instance.applicationPoint = instance.towerBasePoint;
    instance.towerTopDisplacement = {0.1 * surge, 0.1 * sway, 0.1 * heave};
    instance.bladePitch = {pitch, pitch, pitch};
    instance.rotorSpeedRadPerSec = 1.0 + 0.01 * std::abs(vx);
    instance.generatorPowerW = 1.0e4 + 500.0 * std::abs(vx);
    instance.controllerOk = 1;
}

void updateExtLoadsDiagnostics(ForcedOpenFASTInstance& instance)
{
    instance.bladePitch = {0.0, 0.0, 0.0};
    for (int i = 0; i < 3 && i < instance.extLoadsInput.bldPitch_Len; ++i) {
        instance.bladePitch[static_cast<std::size_t>(i)] =
            instance.extLoadsInput.bldPitch[static_cast<std::size_t>(i)];
    }

    instance.towerTopDisplacement = {0.0, 0.0, 0.0};
    if (instance.extLoadsInput.twrDef != nullptr && instance.extLoadsInput.twrDef_Len >= 12) {
        const int nTowerNodes = instance.extLoadsInput.twrDef_Len / 12;
        const int baseIndex = (nTowerNodes - 1) * 12;
        instance.towerTopDisplacement = {
            instance.extLoadsInput.twrDef[static_cast<std::size_t>(baseIndex + 0)],
            instance.extLoadsInput.twrDef[static_cast<std::size_t>(baseIndex + 1)],
            instance.extLoadsInput.twrDef[static_cast<std::size_t>(baseIndex + 2)]
        };
    }
}

bool callOpenFastStatus(
    ForcedOpenFASTInstance& instance,
    const std::string& action,
    const std::function<void(int*, int*, char*)>& invoker)
{
    int iTurb = 0;
    int errStat = 0;
    std::array<char, kInterfaceStringLength> errMsg{};
    invoker(&iTurb, &errStat, errMsg.data());
    if (errStat >= instance.abortErrorLevel || errStat >= 3) {
        std::ostringstream stream;
        stream << action << " failed with OpenFAST error status " << errStat;
        const std::string detail = cStringToStdString(errMsg.data());
        if (!detail.empty()) {
            stream << ": " << detail;
        }
        setError(&instance, stream.str());
        return false;
    }
    return true;
}

bool callBridgeSetMotion(
    ForcedOpenFASTInstance& instance,
    const std::array<double, 6>& displacement,
    const std::array<double, 6>& velocity,
    const std::array<double, 6>& acceleration)
{
    int iTurb = 0;
    int errStat = 0;
    std::array<char, kInterfaceStringLength> errMsg{};
    instance.openfastApi.bridgeSetPlatformMotion(
        &iTurb, displacement.data(), velocity.data(), acceleration.data(), &errStat, errMsg.data());

    if (errStat >= instance.abortErrorLevel || errStat >= 3) {
        std::ostringstream stream;
        stream << "FAST_OC4_Platform_SetMotion failed with OpenFAST error status " << errStat;
        const std::string detail = cStringToStdString(errMsg.data());
        if (!detail.empty()) {
            stream << ": " << detail;
        }
        setError(&instance, stream.str());
        return false;
    }
    return true;
}

bool callBridgeGetLoad(ForcedOpenFASTInstance& instance)
{
    int iTurb = 0;
    int errStat = 0;
    std::array<char, kInterfaceStringLength> errMsg{};
    std::array<double, 3> applicationPoint{};
    std::array<double, 3> force{};
    std::array<double, 3> moment{};

    instance.openfastApi.bridgeGetPlatformReactionLoad(
        &iTurb, applicationPoint.data(), force.data(), moment.data(), &errStat, errMsg.data());

    if (errStat >= instance.abortErrorLevel || errStat >= 3) {
        std::ostringstream stream;
        stream << "FAST_OC4_Platform_GetReactionLoad failed with OpenFAST error status " << errStat;
        const std::string detail = cStringToStdString(errMsg.data());
        if (!detail.empty()) {
            stream << ": " << detail;
        }
        setError(&instance, stream.str());
        return false;
    }

    instance.applicationPoint = applicationPoint;
    instance.force = force;
    instance.moment = moment;
    return true;
}

bool callBridgeGetDiagnostics(ForcedOpenFASTInstance& instance)
{
    if (instance.openfastApi.bridgeGetDiagnostics == nullptr) {
        updateExtLoadsDiagnostics(instance);
        return true;
    }

    int iTurb = 0;
    int controllerOk = 1;
    int errStat = 0;
    std::array<char, kInterfaceStringLength> errMsg{};
    std::array<double, 3> bladePitch{};
    std::array<double, 3> towerTopDisplacement{};
    double rotorSpeed = 0.0;
    double generatorPower = 0.0;

    instance.openfastApi.bridgeGetDiagnostics(
        &iTurb,
        &rotorSpeed,
        &generatorPower,
        bladePitch.data(),
        towerTopDisplacement.data(),
        &controllerOk,
        &errStat,
        errMsg.data());

    if (errStat >= instance.abortErrorLevel || errStat >= 3) {
        std::ostringstream stream;
        stream << "FAST_OC4_Platform_GetDiagnostics failed with OpenFAST error status " << errStat;
        const std::string detail = cStringToStdString(errMsg.data());
        if (!detail.empty()) {
            stream << ": " << detail;
        }
        setError(&instance, stream.str());
        return false;
    }

    instance.rotorSpeedRadPerSec = rotorSpeed;
    instance.generatorPowerW = generatorPower;
    instance.bladePitch = bladePitch;
    instance.towerTopDisplacement = towerTopDisplacement;
    instance.controllerOk = controllerOk;
    return true;
}

std::array<char, kInterfaceStringLength> makeFixedStringBuffer(const std::string& text)
{
    std::array<char, kInterfaceStringLength> buffer{};
    std::strncpy(buffer.data(), text.c_str(), static_cast<std::size_t>(kInterfaceStringLength - 1));
    buffer.back() = '\0';
    return buffer;
}

int initializeStubBackend(
    ForcedOpenFASTInstance& instance,
    const double* displacement,
    const double* velocity,
    const double* acceleration)
{
    copyArray6(displacement, instance.displacement);
    copyArray6(velocity, instance.velocity);
    copyArray6(acceleration, instance.acceleration);
    instance.previousDisplacement = instance.displacement;
    instance.previousVelocity = instance.velocity;
    instance.previousAcceleration = instance.acceleration;
    evaluateStubLoads(instance);
    instance.initialized = true;
    instance.lastError.clear();
    return FORCED_OPENFAST_SUCCESS;
}

int stepStubBackend(
    ForcedOpenFASTInstance& instance,
    const double* displacement,
    const double* velocity,
    const double* acceleration)
{
    copyArray6(displacement, instance.displacement);
    copyArray6(velocity, instance.velocity);
    copyArray6(acceleration, instance.acceleration);
    evaluateStubLoads(instance);
    instance.previousDisplacement = instance.displacement;
    instance.previousVelocity = instance.velocity;
    instance.previousAcceleration = instance.acceleration;
    instance.lastError.clear();
    return FORCED_OPENFAST_SUCCESS;
}

int notImplementedOpenFASTLibraryMessage(ForcedOpenFASTInstance& instance)
{
    std::ostringstream stream;
    stream
        << "backend = \"openfast_library\" is selected and the required stock OpenFAST library "
        << "symbols were found in '" << instance.backendLibraryResolvedPath
        << "', but the OF2-style custom bridge symbols are missing. Stock FAST_ExtLoads_Init "
        << "+ FAST_CFD_* alone expose distributed external-load exchange, not the full imposed-"
        << "platform-motion / returned-interface-wrench contract used by OF2.";
    setError(&instance, stream.str());
    instance.initialized = false;
    return FORCED_OPENFAST_NOT_IMPLEMENTED;
}

int initializeOpenFastLibraryBackend(
    ForcedOpenFASTInstance& instance,
    const double* displacement,
    const double* velocity,
    const double* acceleration)
{
    if (!instance.bridgeAvailable) {
        return notImplementedOpenFASTLibraryMessage(instance);
    }

    int nTurbines = 1;
    int errStat = 0;
    std::array<char, kInterfaceStringLength> errMsg{};
    instance.openfastApi.allocateTurbines(&nTurbines, &errStat, errMsg.data());
    if (errStat >= 3) {
        setError(&instance,
                 "FAST_AllocateTurbines failed: " + cStringToStdString(errMsg.data()));
        return FORCED_OPENFAST_FATAL_ERROR;
    }

    int iTurb = 0;
    auto inputFileBuffer =
        makeFixedStringBuffer((std::filesystem::path(instance.casePath) / instance.openfastInput)
                                  .string());
    std::array<char, kInterfaceStringLength> outFileRootBuffer{};
    int turbineIdForName = 0;
    float turbinePosition[3] = {
        static_cast<float>(instance.platformReferencePoint[0]),
        static_cast<float>(instance.platformReferencePoint[1]),
        static_cast<float>(instance.platformReferencePoint[2]),
    };
    double tMax = instance.tMax;
    double dtDriver = instance.dtOpenFAST * static_cast<double>(instance.substepsPerCouplingStep);
    double dtFromLibrary = 0.0;
    int numBlades = 0;
    int abortErrLev = 0;
    double azBlendMean = 0.0;
    double azBlendDelta = 0.0;

    instance.openfastApi.extLoadsInit(
        &iTurb,
        &tMax,
        inputFileBuffer.data(),
        &turbineIdForName,
        outFileRootBuffer.data(),
        turbinePosition,
        &abortErrLev,
        &dtDriver,
        &dtFromLibrary,
        &numBlades,
        &azBlendMean,
        &azBlendDelta,
        &instance.extLoadsInput,
        &instance.extLoadsParameters,
        &instance.extLoadsOutput,
        &errStat,
        errMsg.data());

    if (errStat >= 3) {
        bool stopTheProgram = false;
        instance.openfastApi.end(&iTurb, &stopTheProgram);
        int deallocErr = 0;
        std::array<char, kInterfaceStringLength> deallocMsg{};
        instance.openfastApi.deallocateTurbines(&deallocErr, deallocMsg.data());
        setError(&instance, "FAST_ExtLoads_Init failed: " + cStringToStdString(errMsg.data()));
        return FORCED_OPENFAST_FATAL_ERROR;
    }

    instance.backendDt = dtFromLibrary;
    instance.numBlades = numBlades;
    instance.abortErrorLevel = abortErrLev > 0 ? abortErrLev : 4;
    instance.outFileRoot = cStringToStdString(outFileRootBuffer.data());

    copyArray6(displacement, instance.displacement);
    copyArray6(velocity, instance.velocity);
    copyArray6(acceleration, instance.acceleration);
    instance.previousDisplacement = instance.displacement;
    instance.previousVelocity = instance.velocity;
    instance.previousAcceleration = instance.acceleration;

    if (!callBridgeSetMotion(instance, instance.displacement, instance.velocity, instance.acceleration) ||
        !callOpenFastStatus(instance,
                            "FAST_CFD_Solution0",
                            [&](int* turbine, int* status, char* message) {
                                instance.openfastApi.cfdSolution0(turbine, status, message);
                            }) ||
        !callBridgeGetLoad(instance) || !callBridgeGetDiagnostics(instance)) {
        bool stopTheProgram = false;
        instance.openfastApi.end(&iTurb, &stopTheProgram);
        int deallocErr = 0;
        std::array<char, kInterfaceStringLength> deallocMsg{};
        instance.openfastApi.deallocateTurbines(&deallocErr, deallocMsg.data());
        instance.initialized = false;
        return FORCED_OPENFAST_FATAL_ERROR;
    }

    instance.initialized = true;
    instance.lastError.clear();
    return FORCED_OPENFAST_SUCCESS;
}

int stepOpenFastLibraryBackend(
    ForcedOpenFASTInstance& instance,
    const double* displacement,
    const double* velocity,
    const double* acceleration)
{
    if (!instance.bridgeAvailable) {
        return notImplementedOpenFASTLibraryMessage(instance);
    }

    copyArray6(displacement, instance.displacement);
    copyArray6(velocity, instance.velocity);
    copyArray6(acceleration, instance.acceleration);

    const int nSubsteps = std::max(instance.substepsPerCouplingStep, 1);
    for (int substep = 0; substep < nSubsteps; ++substep) {
        const double fraction =
            static_cast<double>(substep + 1) / static_cast<double>(nSubsteps);
        const auto substepDisplacement =
            interpolateStateArray(instance.previousDisplacement, instance.displacement, fraction);
        const auto substepVelocity =
            interpolateStateArray(instance.previousVelocity, instance.velocity, fraction);
        const auto substepAcceleration =
            interpolateStateArray(instance.previousAcceleration, instance.acceleration, fraction);

        if (!callOpenFastStatus(instance,
                                "FAST_CFD_Prework",
                                [&](int* turbine, int* status, char* message) {
                                    instance.openfastApi.cfdPrework(turbine, status, message);
                                }) ||
            !callBridgeSetMotion(instance,
                                 substepDisplacement,
                                 substepVelocity,
                                 substepAcceleration) ||
            !callOpenFastStatus(instance,
                                "FAST_CFD_UpdateStates",
                                [&](int* turbine, int* status, char* message) {
                                    instance.openfastApi.cfdUpdateStates(turbine, status, message);
                                }) ||
            !callOpenFastStatus(instance,
                                "FAST_CFD_AdvanceToNextTimeStep",
                                [&](int* turbine, int* status, char* message) {
                                    instance.openfastApi.cfdAdvanceToNextTimeStep(
                                        turbine, status, message);
                                })) {
            return FORCED_OPENFAST_FATAL_ERROR;
        }
    }

    if (!callOpenFastStatus(instance,
                            "FAST_CFD_WriteOutput",
                            [&](int* turbine, int* status, char* message) {
                                instance.openfastApi.cfdWriteOutput(turbine, status, message);
                            }) ||
        !callBridgeGetLoad(instance) || !callBridgeGetDiagnostics(instance)) {
        return FORCED_OPENFAST_FATAL_ERROR;
    }

    instance.previousDisplacement = instance.displacement;
    instance.previousVelocity = instance.velocity;
    instance.previousAcceleration = instance.acceleration;
    instance.lastError.clear();
    return FORCED_OPENFAST_SUCCESS;
}

}  // namespace

extern "C" ForcedOpenFASTHandle ForcedOpenFAST_Create(const char* casePath)
{
    if (casePath == nullptr) {
        return nullptr;
    }

    auto instance = std::make_unique<ForcedOpenFASTInstance>();
    instance->casePath = casePath;

    const std::filesystem::path manifestPath =
        std::filesystem::path(casePath) / "forced_openfast_case.toml";
    instance->manifestPath = manifestPath.string();

    std::string errorMessage;
    if (!parseManifest(manifestPath, *instance, errorMessage)) {
        setError(instance.get(), errorMessage);
        return nullptr;
    }

    if (!validateCommonCaseFiles(*instance, errorMessage)) {
        setError(instance.get(), errorMessage);
        return nullptr;
    }

    if (instance->backend == RuntimeBackend::openfastLibrary &&
        !validateOpenFASTLibraryBackend(*instance, errorMessage)) {
        setError(instance.get(), errorMessage);
        return nullptr;
    }

    return reinterpret_cast<ForcedOpenFASTHandle>(instance.release());
}

extern "C" int ForcedOpenFAST_Initialize(
    ForcedOpenFASTHandle handle,
    const double* displacement,
    const double* velocity,
    const double* acceleration)
{
    auto* instance = fromHandle(handle);
    const int status = validateMotionArrays(instance, displacement, velocity, acceleration);
    if (status != FORCED_OPENFAST_SUCCESS) {
        return status;
    }

    if (instance->backend == RuntimeBackend::stub) {
        return initializeStubBackend(*instance, displacement, velocity, acceleration);
    }

    return initializeOpenFastLibraryBackend(*instance, displacement, velocity, acceleration);
}

extern "C" int ForcedOpenFAST_Step(
    ForcedOpenFASTHandle handle,
    const double* displacement,
    const double* velocity,
    const double* acceleration,
    double* time,
    double* dt)
{
    auto* instance = fromHandle(handle);
    const int status = validateMotionArrays(instance, displacement, velocity, acceleration);
    if (status != FORCED_OPENFAST_SUCCESS) {
        return status;
    }

    if (time == nullptr || dt == nullptr) {
        setError(instance, "ForcedOpenFAST_Step requires non-null time and dt pointers");
        return FORCED_OPENFAST_INVALID_ARGUMENT;
    }

    if (*dt <= 0.0) {
        setError(instance, "ForcedOpenFAST_Step requires dt > 0");
        return FORCED_OPENFAST_INVALID_ARGUMENT;
    }

    if (!instance->initialized) {
        setError(instance, "ForcedOpenFAST_Step called before initialize");
        return FORCED_OPENFAST_FATAL_ERROR;
    }

    if (instance->backend == RuntimeBackend::stub) {
        return stepStubBackend(*instance, displacement, velocity, acceleration);
    }

    return stepOpenFastLibraryBackend(*instance, displacement, velocity, acceleration);
}

extern "C" int ForcedOpenFAST_GetTowerBaseLoad(
    ForcedOpenFASTHandle handle,
    double* applicationPoint,
    double* force,
    double* moment)
{
    auto* instance = fromHandle(handle);
    if (instance == nullptr || applicationPoint == nullptr || force == nullptr || moment == nullptr) {
        return FORCED_OPENFAST_INVALID_ARGUMENT;
    }
    if (!instance->initialized) {
        setError(instance, "ForcedOpenFAST_GetTowerBaseLoad called before a successful initialize()");
        return FORCED_OPENFAST_FATAL_ERROR;
    }

    for (int i = 0; i < 3; ++i) {
        applicationPoint[i] = instance->applicationPoint[static_cast<std::size_t>(i)];
        force[i] = instance->force[static_cast<std::size_t>(i)];
        moment[i] = instance->moment[static_cast<std::size_t>(i)];
    }

    return FORCED_OPENFAST_SUCCESS;
}

extern "C" int ForcedOpenFAST_GetRotorSpeed(
    ForcedOpenFASTHandle handle,
    double* rotorSpeedRadPerSec)
{
    auto* instance = fromHandle(handle);
    if (instance == nullptr || rotorSpeedRadPerSec == nullptr) {
        return FORCED_OPENFAST_INVALID_ARGUMENT;
    }
    if (!instance->initialized) {
        return FORCED_OPENFAST_FATAL_ERROR;
    }

    *rotorSpeedRadPerSec = instance->rotorSpeedRadPerSec;
    return FORCED_OPENFAST_SUCCESS;
}

extern "C" int ForcedOpenFAST_GetGeneratorPower(
    ForcedOpenFASTHandle handle,
    double* generatorPowerW)
{
    auto* instance = fromHandle(handle);
    if (instance == nullptr || generatorPowerW == nullptr) {
        return FORCED_OPENFAST_INVALID_ARGUMENT;
    }
    if (!instance->initialized) {
        return FORCED_OPENFAST_FATAL_ERROR;
    }

    *generatorPowerW = instance->generatorPowerW;
    return FORCED_OPENFAST_SUCCESS;
}

extern "C" int ForcedOpenFAST_GetBladePitch(
    ForcedOpenFASTHandle handle,
    double* bladePitchRad)
{
    auto* instance = fromHandle(handle);
    if (instance == nullptr || bladePitchRad == nullptr) {
        return FORCED_OPENFAST_INVALID_ARGUMENT;
    }
    if (!instance->initialized) {
        return FORCED_OPENFAST_FATAL_ERROR;
    }

    for (int i = 0; i < 3; ++i) {
        bladePitchRad[i] = instance->bladePitch[static_cast<std::size_t>(i)];
    }
    return FORCED_OPENFAST_SUCCESS;
}

extern "C" int ForcedOpenFAST_GetTowerTopDisplacement(
    ForcedOpenFASTHandle handle,
    double* towerTopDisplacementGlobal)
{
    auto* instance = fromHandle(handle);
    if (instance == nullptr || towerTopDisplacementGlobal == nullptr) {
        return FORCED_OPENFAST_INVALID_ARGUMENT;
    }
    if (!instance->initialized) {
        return FORCED_OPENFAST_FATAL_ERROR;
    }

    for (int i = 0; i < 3; ++i) {
        towerTopDisplacementGlobal[i] =
            instance->towerTopDisplacement[static_cast<std::size_t>(i)];
    }
    return FORCED_OPENFAST_SUCCESS;
}

extern "C" int ForcedOpenFAST_GetControllerStatus(
    ForcedOpenFASTHandle handle,
    int* controllerOk)
{
    auto* instance = fromHandle(handle);
    if (instance == nullptr || controllerOk == nullptr) {
        return FORCED_OPENFAST_INVALID_ARGUMENT;
    }
    if (!instance->initialized) {
        return FORCED_OPENFAST_FATAL_ERROR;
    }

    *controllerOk = instance->controllerOk;
    return FORCED_OPENFAST_SUCCESS;
}

extern "C" const char* ForcedOpenFAST_GetLastError(ForcedOpenFASTHandle handle)
{
    auto* instance = fromHandle(handle);
    if (instance == nullptr) {
        return "Invalid ForcedOpenFAST handle";
    }
    return instance->lastError.c_str();
}

extern "C" const char* ForcedOpenFAST_GetVersion(void)
{
    return kVersionString;
}

extern "C" int ForcedOpenFAST_Close(ForcedOpenFASTHandle handle)
{
    auto* instance = fromHandle(handle);
    if (instance == nullptr) {
        return FORCED_OPENFAST_INVALID_ARGUMENT;
    }

    if (instance->backend == RuntimeBackend::openfastLibrary && instance->openfastApi.end &&
        instance->openfastApi.deallocateTurbines && instance->initialized) {
        int iTurb = 0;
        bool stopTheProgram = false;
        instance->openfastApi.end(&iTurb, &stopTheProgram);
        int errStat = 0;
        std::array<char, kInterfaceStringLength> errMsg{};
        instance->openfastApi.deallocateTurbines(&errStat, errMsg.data());
    }

    closeBackendLibrary(*instance);
    delete instance;
    return FORCED_OPENFAST_SUCCESS;
}
