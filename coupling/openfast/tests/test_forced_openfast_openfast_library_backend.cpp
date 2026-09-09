#include "coupling/ForcedOpenFASTCApi.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "Test failure: " << message << '\n';
        std::exit(1);
    }
}

void writeTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream output(path);
    require(output.is_open(), "failed to open file for writing: " + path.string());
    output << contents;
}

bool nearlyEqual(double a, double b, double tolerance = 1e-12)
{
    return std::fabs(a - b) <= tolerance;
}

}  // namespace

int main(int argc, char** argv)
{
    require(argc == 2, "expected fake OpenFAST library path argument");

    const std::filesystem::path tempRoot =
        std::filesystem::temp_directory_path() / "oc4_forced_openfast_library_backend_test";
    std::error_code errorCode;
    std::filesystem::remove_all(tempRoot, errorCode);
    std::filesystem::create_directories(tempRoot / "openfast");

    writeTextFile(tempRoot / "openfast" / "Main.fst", "\"placeholder\"\n");
    writeTextFile(
        tempRoot / "forced_openfast_case.toml",
        std::string("[wrapper]\n") +
            "name = \"oc4_openfast_library_smoke\"\n" +
            "backend = \"openfast_library\"\n" +
            "openfast_input = \"openfast/Main.fst\"\n" +
            "openfast_library_path = \"" + argv[1] + "\"\n" +
            "out_root = \"outputs/openfast_library_smoke\"\n\n" +
            "[coupling]\n" +
            "reaction_load_mode = \"tower_base_wrench\"\n" +
            "platform_reference_point = [0.0, 0.0, -14.46]\n" +
            "tower_base_point = [0.0, 0.0, -5.0]\n" +
            "openfoam_to_openfast_offset = [0.0, 0.0, -15.0]\n\n" +
            "[time]\n" +
            "dt_openfast = 0.01\n" +
            "substeps_per_coupling_step = 1\n" +
            "t_max = 10.0\n" +
            "sim_start = \"init\"\n");

    ForcedOpenFASTHandle handle = ForcedOpenFAST_Create(tempRoot.string().c_str());
    require(handle != nullptr, "openfast_library backend handle should be created");

    const double displacement[6] = {1.0, 2.0, -12.0, 0.1, 0.2, 0.3};
    const double velocity[6] = {4.0, 5.0, 6.0, 0.11, 0.22, 0.33};
    const double acceleration[6] = {0.7, 0.8, 0.9, 0.44, 0.55, 0.66};

    require(ForcedOpenFAST_Initialize(handle, displacement, velocity, acceleration) ==
                FORCED_OPENFAST_SUCCESS,
            "openfast_library bridge initialize should succeed with fake backend");

    double time = 5.0;
    double dt = 0.25;
    require(ForcedOpenFAST_Step(handle, displacement, velocity, acceleration, &time, &dt) ==
                FORCED_OPENFAST_SUCCESS,
            "openfast_library bridge step should succeed");

    double applicationPoint[3] = {};
    double force[3] = {};
    double moment[3] = {};
    require(ForcedOpenFAST_GetTowerBaseLoad(handle, applicationPoint, force, moment) ==
                FORCED_OPENFAST_SUCCESS,
            "load query should succeed");
    require(nearlyEqual(applicationPoint[0], 0.0), "application point x mismatch");
    require(nearlyEqual(applicationPoint[1], 0.0), "application point y mismatch");
    require(nearlyEqual(applicationPoint[2], -5.0), "application point z mismatch");
    require(nearlyEqual(force[0], -42000.0), "force x mismatch");
    require(nearlyEqual(force[1], -75000.0), "force y mismatch");
    require(nearlyEqual(force[2], 228000.0), "force z mismatch");
    require(nearlyEqual(moment[0], -666000.0), "moment x mismatch");
    require(nearlyEqual(moment[1], -1332000.0), "moment y mismatch");
    require(nearlyEqual(moment[2], -999000.0), "moment z mismatch");

    double rotorSpeed = 0.0;
    double generatorPower = 0.0;
    double bladePitch[3] = {};
    double towerTopDisplacement[3] = {};
    int controllerOk = 0;

    require(ForcedOpenFAST_GetRotorSpeed(handle, &rotorSpeed) == FORCED_OPENFAST_SUCCESS,
            "rotor-speed query should succeed");
    require(ForcedOpenFAST_GetGeneratorPower(handle, &generatorPower) == FORCED_OPENFAST_SUCCESS,
            "generator-power query should succeed");
    require(ForcedOpenFAST_GetBladePitch(handle, bladePitch) == FORCED_OPENFAST_SUCCESS,
            "blade-pitch query should succeed");
    require(ForcedOpenFAST_GetTowerTopDisplacement(handle, towerTopDisplacement) ==
                FORCED_OPENFAST_SUCCESS,
            "tower-top displacement query should succeed");
    require(ForcedOpenFAST_GetControllerStatus(handle, &controllerOk) ==
                FORCED_OPENFAST_SUCCESS,
            "controller-status query should succeed");

    require(nearlyEqual(rotorSpeed, 2.08), "rotor speed mismatch");
    require(nearlyEqual(generatorPower, 24000.0), "generator power mismatch");
    require(nearlyEqual(bladePitch[0], 0.2), "blade pitch 0 mismatch");
    require(nearlyEqual(bladePitch[1], 0.2), "blade pitch 1 mismatch");
    require(nearlyEqual(bladePitch[2], 0.2), "blade pitch 2 mismatch");
    require(nearlyEqual(towerTopDisplacement[0], 0.2), "tower-top displacement x mismatch");
    require(nearlyEqual(towerTopDisplacement[1], 0.4), "tower-top displacement y mismatch");
    require(nearlyEqual(towerTopDisplacement[2], -2.4), "tower-top displacement z mismatch");
    require(controllerOk == 1, "controller status mismatch");

    require(ForcedOpenFAST_Close(handle) == FORCED_OPENFAST_SUCCESS, "close should succeed");

    std::filesystem::remove_all(tempRoot, errorCode);
    std::cout << "All openfast_library backend tests passed.\n";
    return 0;
}
