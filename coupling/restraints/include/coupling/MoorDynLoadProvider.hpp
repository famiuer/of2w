#pragma once

#include "coupling/ExternalLoadProvider.hpp"
#include "coupling/MoorDynAdapter.hpp"

#include <fstream>
#include <memory>
#include <string>

namespace oc4::coupling {

struct MoorDynLoadProviderConfig {
    MoorDynConfig moorDyn{};
    bool shiftResultToPlatformReferencePoint{true};
    std::string debugCsvPath{};
};

class MoorDynLoadProvider final : public ExternalLoadProvider {
public:
    MoorDynLoadProvider(
        MoorDynLoadProviderConfig config,
        std::unique_ptr<MoorDynAdapter> adapter);
    ~MoorDynLoadProvider() override;

    void initialize(const PlatformState& initialState) override;
    Wrench evaluate(const PlatformState& platformState, double dt) override;
    void close() override;

private:
    MoorDynLoadProviderConfig config_{};
    std::unique_ptr<MoorDynAdapter> adapter_{};
    bool initialized_{false};
    std::ofstream debugStream_{};

    void openDebugLog();
    void writeDebugHeader();
    void writeDebugRow(
        const PlatformState& platformState,
        double dt,
        const MoorDynResult& result,
        const Wrench& appliedLoad);
};

}  // namespace oc4::coupling
