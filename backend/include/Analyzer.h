#pragma once

#include <memory>
#include <vector>
#include <string>
#include "Event.h"
#include "AnalysisStrategy.h"
#include "third_party/json.hpp"

namespace tracex {

struct WhatIfResult {
    AnalysisResult originalResult;
    AnalysisResult modifiedResult;
    std::vector<int> removedEventIds;
    bool rootCauseChanged;
    double confidenceDelta;
    std::string diffSummary;

    nlohmann::json toJson() const {
        return {
            {"original", originalResult.toJson()},
            {"modified", modifiedResult.toJson()},
            {"removed_event_ids", removedEventIds},
            {"root_cause_changed", rootCauseChanged},
            {"confidence_delta", std::round(confidenceDelta * 10.0) / 10.0},
            {"diff_summary", diffSummary}
        };
    }
};

struct AlgorithmComparisonItem {
    std::string strategyName;
    std::string rootCause;
    int rootEventId;
    double confidence;
    size_t chainsCount;
    size_t supportingCount;
    double executionTimeMs;

    nlohmann::json toJson() const {
        return {
            {"strategy", strategyName},
            {"root_cause", rootCause},
            {"root_event_id", rootEventId},
            {"confidence", std::round(confidence * 10.0) / 10.0},
            {"chains_count", chainsCount},
            {"supporting_count", supportingCount},
            {"exec_ms", std::round(executionTimeMs * 1000.0) / 1000.0}
        };
    }
};

struct AlgorithmComparisonResult {
    std::vector<AlgorithmComparisonItem> comparisons;
    std::string winnerSummary;

    nlohmann::json toJson() const {
        nlohmann::json items = nlohmann::json::array();
        for (const auto& item : comparisons) {
            items.push_back(item.toJson());
        }
        return {
            {"comparisons", items},
            {"summary", winnerSummary}
        };
    }
};

/**
 * Analyzer Context:
 * Holds the current AnalysisStrategy (Strategy Pattern) and performs
 * Failure-Chain correlation, What-If simulation, and Algorithm Benchmarking.
 */
class Analyzer {
private:
    std::unique_ptr<AnalysisStrategy> strategy_;

public:
    Analyzer();
    explicit Analyzer(std::unique_ptr<AnalysisStrategy> strategy);

    void setStrategy(std::unique_ptr<AnalysisStrategy> strategy);
    const AnalysisStrategy* getStrategy() const noexcept { return strategy_.get(); }
    std::string getStrategyName() const;

    // Analyze events
    AnalysisResult analyze(const std::vector<std::unique_ptr<Event>>& events);
    AnalysisResult analyze(const std::vector<Event*>& eventPtrs);

    // What-If Simulation
    WhatIfResult runWhatIf(const std::vector<std::unique_ptr<Event>>& events, const std::vector<int>& removeEventIds);

    // Algorithm Comparison
    AlgorithmComparisonResult compareAlgorithms(const std::vector<std::unique_ptr<Event>>& events);
};

} // namespace tracex
