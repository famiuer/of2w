#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

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

struct FakeState {
    std::array<double, 6> displacement{};
    std::array<double, 6> velocity{};
    std::array<double, 6> acceleration{};
    std::array<double, 3> applicationPoint{0.0, 0.0, -5.0};
    std::array<double, 3> force{};
    std::array<double, 3> moment{};
    std::array<double, 3> bladePitch{};
    std::array<double, 3> towerTopDisplacement{};
    double rotorSpeed{0.0};
    double generatorPower{0.0};
    int controllerOk{1};
    int allocateCount{0};
    int initCount{0};
    int preworkCount{0};
    int updateCount{0};
    int advanceCount{0};
    int writeCount{0};
    int solution0Count{0};
    std::array<double, 24> twrDef{};
    std::array<double, 36> bldDef{};
    std::array<double, 12> hubDef{};
    std::array<double, 12> nacDef{};
    std::array<double, 36> bldRootDef{};
    std::array<double, 3> bldPitchArray{};
    std::array<int, 1> nBlades{3};
    std::array<int, 3> nBladeNodes{2, 2, 2};
    std::array<int, 1> nTowerNodes{2};
    std::array<double, 12> twrRefPos{};
    std::array<double, 36> bldRefPos{};
    std::array<double, 6> hubRefPos{};
    std::array<double, 6> nacRefPos{};
    std::array<double, 18> bldRootRefPos{};
    std::array<double, 6> bldChord{};
    std::array<double, 6> bldRloc{};
    std::array<double, 2> twrDia{};
    std::array<double, 2> twrHloc{};
    std::array<double, 12> twrLd{};
    std::array<double, 36> bldLd{};
} gState;

void setMessage(char* errMsg, const char* message)
{
    if (errMsg == nullptr) {
        return;
    }
    std::memset(errMsg, 0, static_cast<std::size_t>(kInterfaceStringLength));
    if (message != nullptr) {
        std::strncpy(errMsg, message, static_cast<std::size_t>(kInterfaceStringLength - 1));
    }
}

void evaluateLoads()
{
    const double surge = gState.displacement[0];
    const double sway = gState.displacement[1];
    const double heave = gState.displacement[2];
    const double roll = gState.displacement[3];
    const double pitch = gState.displacement[4];
    const double yaw = gState.displacement[5];

    const double vx = gState.velocity[0];
    const double vy = gState.velocity[1];
    const double vz = gState.velocity[2];
    const double wx = gState.velocity[3];
    const double wy = gState.velocity[4];
    const double wz = gState.velocity[5];

    gState.force = {
        -3.0e4 * surge - 3.0e3 * vx,
        -3.0e4 * sway - 3.0e3 * vy,
        -2.0e4 * heave - 2.0e3 * vz
    };
    gState.moment = {
        -6.0e6 * roll - 6.0e5 * wx,
        -6.0e6 * pitch - 6.0e5 * wy,
        -3.0e6 * yaw - 3.0e5 * wz
    };

    gState.towerTopDisplacement = {0.2 * surge, 0.2 * sway, 0.2 * heave};
    gState.bladePitch = {pitch, pitch, pitch};
    gState.rotorSpeed = 2.0 + 0.02 * std::abs(vx);
    gState.generatorPower = 2.0e4 + 1000.0 * std::abs(vx);
    gState.controllerOk = 1;

    std::fill(gState.twrDef.begin(), gState.twrDef.end(), 0.0);
    gState.twrDef[12] = gState.towerTopDisplacement[0];
    gState.twrDef[13] = gState.towerTopDisplacement[1];
    gState.twrDef[14] = gState.towerTopDisplacement[2];
    gState.twrDef[18] = roll;
    gState.twrDef[19] = pitch;
    gState.twrDef[20] = yaw;

    gState.bldPitchArray = gState.bladePitch;
}

}  // namespace

extern "C" {

void FAST_AllocateTurbines(int* nTurbines, int* errStat, char* errMsg)
{
    gState.allocateCount += 1;
    if (nTurbines != nullptr && *nTurbines == 1) {
        *errStat = 0;
    } else {
        *errStat = 4;
    }
    setMessage(errMsg, "");
}

void FAST_DeallocateTurbines(int* errStat, char* errMsg)
{
    if (errStat != nullptr) {
        *errStat = 0;
    }
    setMessage(errMsg, "");
}

void FAST_ExtLoads_Init(
    int*,
    double*,
    const char*,
    int*,
    char* outFileRoot,
    float*,
    int* abortErrLev,
    double*,
    double* dt,
    int* numBlades,
    double*,
    double*,
    ExtLdDX_InputType_t* extLdInput,
    ExtLdDX_ParameterType_t* extLdParameters,
    ExtLdDX_OutputType_t* extLdOutput,
    int* errStat,
    char* errMsg)
{
    gState.initCount += 1;
    if (abortErrLev != nullptr) {
        *abortErrLev = 4;
    }
    if (dt != nullptr) {
        *dt = 0.01;
    }
    if (numBlades != nullptr) {
        *numBlades = 3;
    }
    if (outFileRoot != nullptr) {
        setMessage(outFileRoot, "fake_openfast");
    }

    extLdInput->twrDef = gState.twrDef.data();
    extLdInput->twrDef_Len = static_cast<int>(gState.twrDef.size());
    extLdInput->bldDef = gState.bldDef.data();
    extLdInput->bldDef_Len = static_cast<int>(gState.bldDef.size());
    extLdInput->hubDef = gState.hubDef.data();
    extLdInput->hubDef_Len = static_cast<int>(gState.hubDef.size());
    extLdInput->nacDef = gState.nacDef.data();
    extLdInput->nacDef_Len = static_cast<int>(gState.nacDef.size());
    extLdInput->bldRootDef = gState.bldRootDef.data();
    extLdInput->bldRootDef_Len = static_cast<int>(gState.bldRootDef.size());
    extLdInput->bldPitch = gState.bldPitchArray.data();
    extLdInput->bldPitch_Len = static_cast<int>(gState.bldPitchArray.size());

    extLdParameters->nBlades = gState.nBlades.data();
    extLdParameters->nBlades_Len = static_cast<int>(gState.nBlades.size());
    extLdParameters->nBladeNodes = gState.nBladeNodes.data();
    extLdParameters->nBladeNodes_Len = static_cast<int>(gState.nBladeNodes.size());
    extLdParameters->nTowerNodes = gState.nTowerNodes.data();
    extLdParameters->nTowerNodes_Len = static_cast<int>(gState.nTowerNodes.size());
    extLdParameters->twrRefPos = gState.twrRefPos.data();
    extLdParameters->twrRefPos_Len = static_cast<int>(gState.twrRefPos.size());
    extLdParameters->bldRefPos = gState.bldRefPos.data();
    extLdParameters->bldRefPos_Len = static_cast<int>(gState.bldRefPos.size());
    extLdParameters->hubRefPos = gState.hubRefPos.data();
    extLdParameters->hubRefPos_Len = static_cast<int>(gState.hubRefPos.size());
    extLdParameters->nacRefPos = gState.nacRefPos.data();
    extLdParameters->nacRefPos_Len = static_cast<int>(gState.nacRefPos.size());
    extLdParameters->bldRootRefPos = gState.bldRootRefPos.data();
    extLdParameters->bldRootRefPos_Len = static_cast<int>(gState.bldRootRefPos.size());
    extLdParameters->bldChord = gState.bldChord.data();
    extLdParameters->bldChord_Len = static_cast<int>(gState.bldChord.size());
    extLdParameters->bldRloc = gState.bldRloc.data();
    extLdParameters->bldRloc_Len = static_cast<int>(gState.bldRloc.size());
    extLdParameters->twrDia = gState.twrDia.data();
    extLdParameters->twrDia_Len = static_cast<int>(gState.twrDia.size());
    extLdParameters->twrHloc = gState.twrHloc.data();
    extLdParameters->twrHloc_Len = static_cast<int>(gState.twrHloc.size());

    extLdOutput->twrLd = gState.twrLd.data();
    extLdOutput->twrLd_Len = static_cast<int>(gState.twrLd.size());
    extLdOutput->bldLd = gState.bldLd.data();
    extLdOutput->bldLd_Len = static_cast<int>(gState.bldLd.size());

    if (errStat != nullptr) {
        *errStat = 0;
    }
    setMessage(errMsg, "");
}

void FAST_CFD_Solution0(int*, int* errStat, char* errMsg)
{
    gState.solution0Count += 1;
    if (errStat != nullptr) {
        *errStat = 0;
    }
    setMessage(errMsg, "");
}

void FAST_CFD_Prework(int*, int* errStat, char* errMsg)
{
    gState.preworkCount += 1;
    if (errStat != nullptr) {
        *errStat = 0;
    }
    setMessage(errMsg, "");
}

void FAST_CFD_UpdateStates(int*, int* errStat, char* errMsg)
{
    gState.updateCount += 1;
    evaluateLoads();
    if (errStat != nullptr) {
        *errStat = 0;
    }
    setMessage(errMsg, "");
}

void FAST_CFD_AdvanceToNextTimeStep(int*, int* errStat, char* errMsg)
{
    gState.advanceCount += 1;
    if (errStat != nullptr) {
        *errStat = 0;
    }
    setMessage(errMsg, "");
}

void FAST_CFD_WriteOutput(int*, int* errStat, char* errMsg)
{
    gState.writeCount += 1;
    if (errStat != nullptr) {
        *errStat = 0;
    }
    setMessage(errMsg, "");
}

void FAST_End(int*, bool*)
{
}

void FAST_OC4_Platform_SetMotion(
    int*,
    const double* displacement,
    const double* velocity,
    const double* acceleration,
    int* errStat,
    char* errMsg)
{
    std::copy(displacement, displacement + 6, gState.displacement.begin());
    std::copy(velocity, velocity + 6, gState.velocity.begin());
    std::copy(acceleration, acceleration + 6, gState.acceleration.begin());
    if (errStat != nullptr) {
        *errStat = 0;
    }
    setMessage(errMsg, "");
}

void FAST_OC4_Platform_GetReactionLoad(
    int*,
    double* applicationPoint,
    double* force,
    double* moment,
    int* errStat,
    char* errMsg)
{
    for (int i = 0; i < 3; ++i) {
        applicationPoint[i] = gState.applicationPoint[static_cast<std::size_t>(i)];
        force[i] = gState.force[static_cast<std::size_t>(i)];
        moment[i] = gState.moment[static_cast<std::size_t>(i)];
    }
    if (errStat != nullptr) {
        *errStat = 0;
    }
    setMessage(errMsg, "");
}

void FAST_OC4_Platform_GetDiagnostics(
    int*,
    double* rotorSpeed,
    double* generatorPower,
    double* bladePitch,
    double* towerTopDisplacement,
    int* controllerOk,
    int* errStat,
    char* errMsg)
{
    *rotorSpeed = gState.rotorSpeed;
    *generatorPower = gState.generatorPower;
    for (int i = 0; i < 3; ++i) {
        bladePitch[i] = gState.bladePitch[static_cast<std::size_t>(i)];
        towerTopDisplacement[i] = gState.towerTopDisplacement[static_cast<std::size_t>(i)];
    }
    *controllerOk = gState.controllerOk;
    if (errStat != nullptr) {
        *errStat = 0;
    }
    setMessage(errMsg, "");
}

}
