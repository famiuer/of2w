#pragma once

#include "coupling/ExternalLoadProvider.hpp"
#include "coupling/OpenFASTAdapter.hpp"

#include <fstream>
#include <memory>
#include <string>

namespace oc4::coupling {

struct OpenFASTLoadProviderConfig {
    OpenFASTConfig openFast{};
    bool shiftResultToPlatformReferencePoint{true};
    std::string debugCsvPath{};
};

class OpenFASTLoadProvider final : public ExternalLoadProvider {
public:
    OpenFASTLoadProvider(
        OpenFASTLoadProviderConfig config,
        std::unique_ptr<OpenFASTAdapter> adapter);
    ~OpenFASTLoadProvider() override;

    void initialize(const PlatformState& initialState) override;
    Wrench evaluate(const PlatformState& platformState, double dt) override;
    void close() override;

private:
    OpenFASTLoadProviderConfig config_{};
    std::unique_ptr<OpenFASTAdapter> adapter_{};
    bool initialized_{false};
    std::ofstream debugStream_{};

    void openDebugLog();
    void writeDebugHeader();
    void writeDebugRow(
        const PlatformState& platformState,
        double dt,
        const OpenFASTResult& result,
        const Wrench& appliedLoad);
};

}  // namespace oc4::coupling
