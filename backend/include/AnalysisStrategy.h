#pragma once

#include <vector>
#include <string>
#include <memory>
#include "Event.h"
#include "third_party/json.hpp"

namespace tracex {

struct GraphNode {
    int id;
    std::string label;
    std::string level;
    std::string source;
    std::string type;
    std::string timestamp;
    bool isRoot;
    bool isSupporting;

    nlohmann::json toJson() const {
        return {
            {"id", id},
            {"label", label},
            {"level", level},
            {"source", source},
            {"type", type},
            {"timestamp", timestamp},
            {"isRoot", isRoot},
            {"isSupporting", isSupporting}
        };
    }
};

struct GraphEdge {
    int from;
    int to;
    std::string label;
    std::string relation;

    nlohmann::json toJson() const {
        return {
            {"from", from},
            {"to", to},
            {"label", label},
            {"relation", relation}
        };
    }
};

struct AnalysisResult {
    std::string strategyName;
    std::string rootCauseType;
    int rootEventId = 0;
    std::string rootEventSummary;
    double confidence = 0.0; // 0.0 to 100.0%
    std::string explanation; // "Possible root cause: ... (evidence-based hypothesis)"
    std::vector<int> supportingEventIds;
    std::vector<std::vector<int>> failureChains;
    std::vector<GraphNode> graphNodes;
    std::vector<GraphEdge> graphEdges;
    double executionTimeMs = 0.0;
    size_t totalErrorsAnalyzed = 0;

    nlohmann::json toJson() const;
};

/**
 * Abstract Base Strategy for Root Cause Analysis.
 * Implements the Strategy Pattern and Polymorphism.
 */
class AnalysisStrategy {
public:
    virtual ~AnalysisStrategy() = default;
    virtual AnalysisResult analyze(const std::vector<Event*>& sortedEvents) = 0;
    virtual std::string name() const = 0;

protected:
    double calculateConfidence(double temporalPrecedence, double downstreamRatio, double ruleMatch) const {
        // confidence = (0.4 * temporal_precedence + 0.3 * downstream_ratio + 0.3 * rule_match) * 100%
        double score = (0.4 * temporalPrecedence + 0.3 * downstreamRatio + 0.3 * ruleMatch) * 100.0;
        if (score < 0.0) score = 0.0;
        if (score > 100.0) score = 100.0;
        return score;
    }
};

/**
 * RuleBasedStrategy:
 * Applies domain-specific causality rules:
 * - DB_FAILURE followed by >= 2 API_FAILURE within 10s -> root cause DB
 * - NETWORK_FAILURE followed by DB/API failures within 10s -> root cause NETWORK
 * - MEMORY_PRESSURE followed by timeouts/crashes within 15s -> root cause MEMORY
 * - AUTH_FAILURE followed by API 401/403 failures within 10s -> root cause AUTH
 */
class RuleBasedStrategy : public AnalysisStrategy {
public:
    AnalysisResult analyze(const std::vector<Event*>& sortedEvents) override;
    std::string name() const override { return "Rule-Based Correlation Strategy"; }
};

/**
 * TimeWindowStrategy:
 * Slides a temporal window across error events. In each window with >= 3 errors,
 * identifies the earliest error as the candidate root and scores it by downstream cascade size.
 */
class TimeWindowStrategy : public AnalysisStrategy {
private:
    int windowSeconds_;

public:
    explicit TimeWindowStrategy(int windowSeconds = 15) : windowSeconds_(windowSeconds) {}
    AnalysisResult analyze(const std::vector<Event*>& sortedEvents) override;
    std::string name() const override { return "Sliding Time-Window Strategy"; }
};

} // namespace tracex
