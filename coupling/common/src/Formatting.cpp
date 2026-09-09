#include "coupling/Formatting.hpp"

#include <iomanip>
#include <sstream>

namespace oc4::coupling {

namespace {

std::string formatDouble(double value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << value;
    return stream.str();
}

}  // namespace

std::string toString(FrameId frameId)
{
    switch (frameId) {
    case FrameId::unknown:
        return "unknown";
    case FrameId::global:
        return "global";
    case FrameId::platformBody:
        return "platformBody";
    }

    return "invalid";
}

std::string toString(const Vector3& vector)
{
    std::ostringstream stream;
    stream << "("
           << formatDouble(vector.x) << ", "
           << formatDouble(vector.y) << ", "
           << formatDouble(vector.z) << ")";
    return stream.str();
}

std::string toString(const ReferencePoint& referencePoint)
{
    std::ostringstream stream;
    stream << referencePoint.name
           << " pos=" << toString(referencePoint.position)
           << " frame=" << toString(referencePoint.frame);
    return stream.str();
}

std::string toString(const Wrench& wrench)
{
    std::ostringstream stream;
    stream << "force=" << toString(wrench.force)
           << " moment=" << toString(wrench.moment)
           << " point={" << toString(wrench.applicationPoint) << "}"
           << " expressedIn=" << toString(wrench.expressedInFrame);
    return stream.str();
}

std::string toString(const PlatformState& state)
{
    std::ostringstream stream;
    stream << "t=" << formatDouble(state.time)
           << " ref={" << toString(state.referencePoint) << "}"
           << " pos=" << toString(state.positionGlobal)
           << " vel=" << toString(state.velocityGlobal)
           << " acc=" << toString(state.accelerationGlobal)
           << " omega=" << toString(state.angularVelocityGlobal)
           << " alpha=" << toString(state.angularAccelerationGlobal)
           << " euler=" << toString(state.eulerAnglesRad)
           << " eulerDot=" << toString(state.eulerAngleRatesRadPerSec);
    return stream.str();
}

}  // namespace oc4::coupling
