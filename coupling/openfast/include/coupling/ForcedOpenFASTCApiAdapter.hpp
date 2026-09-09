#pragma once

#include "coupling/OpenFASTAdapter.hpp"
#include "coupling/OpenFASTApi.hpp"

#include <array>

namespace oc4::coupling {

class ForcedOpenFASTCApiAdapter final : public OpenFASTAdapter {
public:
    ForcedOpenFASTCApiAdapter();
    explicit ForcedOpenFASTCApiAdapter(OpenFASTApi api);
    ~ForcedOpenFASTCApiAdapter() override;

    void initialize(const OpenFASTConfig& config, const PlatformState& initialState) override;
    OpenFASTResult step(const PlatformState& platformState, double dt) override;
    void close() override;

private:
    OpenFASTApi api_{};
    OpenFASTHandle handle_{nullptr};
    OpenFASTConfig config_{};
    bool initialized_{false};

    std::array<double, 6> buildDisplacementStateVector(const PlatformState& platformState) const;
    static std::array<double, 6> buildVelocityStateVector(const PlatformState& platformState);
    static std::array<double, 6> buildAccelerationStateVector(const PlatformState& platformState);
    static void validatePlatformState(const PlatformState& platformState);
};

}  // namespace oc4::coupling
