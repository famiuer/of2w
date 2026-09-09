#pragma once

#include "coupling/MoorDynAdapter.hpp"
#include "coupling/MoorDynApi.hpp"

#include <array>

namespace oc4::coupling {

class MoorDynCApiAdapter final : public MoorDynAdapter {
public:
    MoorDynCApiAdapter();
    explicit MoorDynCApiAdapter(MoorDynApi api);
    ~MoorDynCApiAdapter() override;

    void initialize(const MoorDynConfig& config, const PlatformState& initialState) override;
    MoorDynResult step(const PlatformState& platformState, double dt) override;
    void close() override;

private:
    MoorDynApi api_{};
    bool apiExternallyProvided_{false};
    MoorDynHandle system_{nullptr};
    MoorDynConfig config_{};
    bool initialized_{false};
    std::vector<Vector3> externalWaveSamplePointsGlobal_{};

    std::array<double, 6> buildPositionStateVector(const PlatformState& platformState) const;
    static std::array<double, 6> buildVelocityStateVector(const PlatformState& platformState);
    static void validatePlatformState(const PlatformState& platformState);
    void configureExternalWaveKinematics();
    void setExternalWaveKinematics(double time);
};

}  // namespace oc4::coupling
