#pragma once

#include "coupling/Types.hpp"

namespace oc4::coupling {

class ExternalLoadProvider {
public:
    virtual ~ExternalLoadProvider() = default;

    virtual void initialize(const PlatformState& initialState) = 0;
    virtual Wrench evaluate(const PlatformState& platformState, double dt) = 0;
    virtual void close() = 0;
};

}  // namespace oc4::coupling
