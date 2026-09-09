#pragma once

#include "coupling/Types.hpp"

#include <string>

namespace oc4::coupling {

std::string toString(FrameId frameId);
std::string toString(const Vector3& vector);
std::string toString(const ReferencePoint& referencePoint);
std::string toString(const Wrench& wrench);
std::string toString(const PlatformState& state);

}  // namespace oc4::coupling
