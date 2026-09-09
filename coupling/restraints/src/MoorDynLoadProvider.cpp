#include "coupling/MoorDynLoadProvider.hpp"

#include "coupling/Math.hpp"

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

}  // namespace

MoorDynLoadProvider::MoorDynLoadProvider(
    MoorDynLoadProviderConfig config,
    std::unique_ptr<MoorDynAdapter> adapter)
    : config_(std::move(config))
    , adapter_(std::move(adapter))
{
    require(static_cast<bool>(adapter_), "MoorDynLoadProvider requires a non-null adapter");
}

MoorDynLoadProvider::~MoorDynLoadProvider()
{
    close();
}

void MoorDynLoadProvider::initialize(const PlatformState& initialState)
{
    close();

    require(config_.moorDyn.platformReferencePoint.frame == FrameId::global,
            "MoorDynLoadProvider requires a global-frame platform reference point");

    adapter_->initialize(config_.moorDyn, initialState);
    openDebugLog();
    initialized_ = true;
}

Wrench MoorDynLoadProvider::evaluate(const PlatformState& platformState, double dt)
{
    require(initialized_, "MoorDynLoadProvider evaluate() called before initialize()");

    const MoorDynResult result = adapter_->step(platformState, dt);
    Wrench appliedLoad = result.resultantLoad;

    if (config_.shiftResultToPlatformReferencePoint) {
        appliedLoad = shiftWrenchReferencePoint(result.resultantLoad, platformState.referencePoint);
    }

    writeDebugRow(platformState, dt, result, appliedLoad);
    return appliedLoad;
}

void MoorDynLoadProvider::close()
{
    if (adapter_) {
        adapter_->close();
    }

    if (debugStream_.is_open()) {
        debugStream_.close();
    }

    initialized_ = false;
}

void MoorDynLoadProvider::openDebugLog()
{
    if (config_.debugCsvPath.empty()) {
        return;
    }

    debugStream_.open(config_.debugCsvPath, std::ios::out | std::ios::trunc);
    if (!debugStream_.is_open()) {
        throw std::runtime_error(
            "Failed to open MoorDyn debug log at '" + config_.debugCsvPath + "'");
    }

    writeDebugHeader();
}

void MoorDynLoadProvider::writeDebugHeader()
{
    if (!debugStream_.is_open()) {
        return;
    }

    debugStream_
        << "time,dt,"
        << "platform_ref_x,platform_ref_y,platform_ref_z,"
        << "platform_roll,platform_pitch,platform_yaw,"
        << "platform_vx,platform_vy,platform_vz,"
        << "platform_roll_dot,platform_pitch_dot,platform_yaw_dot,"
        << "result_force_x,result_force_y,result_force_z,"
        << "result_moment_x,result_moment_y,result_moment_z,"
        << "result_point_x,result_point_y,result_point_z,"
        << "applied_force_x,applied_force_y,applied_force_z,"
        << "applied_moment_x,applied_moment_y,applied_moment_z,"
        << "applied_point_x,applied_point_y,applied_point_z,"
        << "status_code,status_message\n";
}

void MoorDynLoadProvider::writeDebugRow(
    const PlatformState& platformState,
    double dt,
    const MoorDynResult& result,
    const Wrench& appliedLoad)
{
    if (!debugStream_.is_open()) {
        return;
    }

    const auto& platformPoint = platformState.referencePoint.position;
    const auto& resultPoint = result.resultantLoad.applicationPoint.position;
    const auto& appliedPoint = appliedLoad.applicationPoint.position;

    debugStream_
        << platformState.time << ','
        << dt << ','
        << platformPoint.x << ','
        << platformPoint.y << ','
        << platformPoint.z << ','
        << platformState.eulerAnglesRad.x << ','
        << platformState.eulerAnglesRad.y << ','
        << platformState.eulerAnglesRad.z << ','
        << platformState.velocityGlobal.x << ','
        << platformState.velocityGlobal.y << ','
        << platformState.velocityGlobal.z << ','
        << platformState.eulerAngleRatesRadPerSec.x << ','
        << platformState.eulerAngleRatesRadPerSec.y << ','
        << platformState.eulerAngleRatesRadPerSec.z << ','
        << result.resultantLoad.force.x << ','
        << result.resultantLoad.force.y << ','
        << result.resultantLoad.force.z << ','
        << result.resultantLoad.moment.x << ','
        << result.resultantLoad.moment.y << ','
        << result.resultantLoad.moment.z << ','
        << resultPoint.x << ','
        << resultPoint.y << ','
        << resultPoint.z << ','
        << appliedLoad.force.x << ','
        << appliedLoad.force.y << ','
        << appliedLoad.force.z << ','
        << appliedLoad.moment.x << ','
        << appliedLoad.moment.y << ','
        << appliedLoad.moment.z << ','
        << appliedPoint.x << ','
        << appliedPoint.y << ','
        << appliedPoint.z << ','
        << result.statusCode << ','
        << '"' << result.statusMessage << '"' << '\n';
}

}  // namespace oc4::coupling
