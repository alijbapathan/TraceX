#include "Analyzer.h"
#include "Utils.h"
#include <set>
#include <sstream>

namespace tracex {

Analyzer::Analyzer()
    : strategy_(std::make_unique<RuleBasedStrategy>()) {}

Analyzer::Analyzer(std::unique_ptr<AnalysisStrategy> strategy)
    : strategy_(std::move(strategy)) {
    if (!strategy_) {
        strategy_ = std::make_unique<RuleBasedStrategy>();
    }
}

void Analyzer::setStrategy(std::unique_ptr<AnalysisStrategy> strategy) {
    if (strategy) {
        strategy_ = std::move(strategy);
    }
}

std::string Analyzer::getStrategyName() const {
    return strategy_ ? strategy_->name() : "None";
}

AnalysisResult Analyzer::analyze(const std::vector<std::unique_ptr<Event>>& events) {
    std::vector<Event*> ptrs;
    ptrs.reserve(events.size());
    for (const auto& ev : events) {
        if (ev) ptrs.push_back(ev.get());
    }
    return analyze(ptrs);
}

AnalysisResult Analyzer::analyze(const std::vector<Event*>& eventPtrs) {
    // Sort events chronologically using generic template utility
    std::vector<Event*> sorted = eventPtrs;
    sortBy(sorted, [](Event* a, Event* b) {
        if (a->timestamp != b->timestamp) {
            return a->timestamp < b->timestamp;
        }
        return a->id < b->id;
    });

    return strategy_->analyze(sorted);
}

WhatIfResult Analyzer::runWhatIf(const std::vector<std::unique_ptr<Event>>& events, const std::vector<int>& removeEventIds) {
    // 1. Analyze original
    AnalysisResult orig = analyze(events);

    // 2. Perform deep copy of events, excluding the removed event IDs
    std::set<int> toRemove(removeEventIds.begin(), removeEventIds.end());
    std::vector<std::unique_ptr<Event>> modifiedEvents;
    modifiedEvents.reserve(events.size());

    for (const auto& ev : events) {
        if (ev && toRemove.find(ev->id) == toRemove.end()) {
            modifiedEvents.push_back(ev->clone());
        }
    }

    // 3. Re-run analysis on modified set
    AnalysisResult mod = analyze(modifiedEvents);

    WhatIfResult whatif;
    whatif.originalResult = orig;
    whatif.modifiedResult = mod;
    whatif.removedEventIds = removeEventIds;
    whatif.rootCauseChanged = (orig.rootCauseType != mod.rootCauseType) || (orig.rootEventId != mod.rootEventId);
    whatif.confidenceDelta = mod.confidence - orig.confidence;

    std::ostringstream diff;
    if (whatif.rootCauseChanged) {
        diff << "Root cause shifted from " << orig.rootCauseType << " (#" << orig.rootEventId 
             << ") to " << mod.rootCauseType << " (#" << mod.rootEventId << "). ";
    } else {
        diff << "Root cause remained unchanged (" << orig.rootCauseType << "). ";
    }

    if (std::abs(whatif.confidenceDelta) > 0.01) {
        diff << "Confidence score shifted by " << (whatif.confidenceDelta > 0 ? "+" : "") 
             << std::round(whatif.confidenceDelta * 10.0) / 10.0 << "% (now " 
             << std::round(mod.confidence * 10.0) / 10.0 << "%). ";
    }

    diff << "Removed " << removeEventIds.size() << " event(s). Failure chains went from " 
         << orig.failureChains.size() << " to " << mod.failureChains.size() << ".";
    whatif.diffSummary = diff.str();

    return whatif;
}

AlgorithmComparisonResult Analyzer::compareAlgorithms(const std::vector<std::unique_ptr<Event>>& events) {
    AlgorithmComparisonResult compResult;

    // Run Rule-Based Strategy
    RuleBasedStrategy ruleStrat;
    std::vector<Event*> ptrs;
    ptrs.reserve(events.size());
    for (const auto& ev : events) {
        if (ev) ptrs.push_back(ev.get());
    }
    sortBy(ptrs, [](Event* a, Event* b) {
        if (a->timestamp != b->timestamp) return a->timestamp < b->timestamp;
        return a->id < b->id;
    });

    AnalysisResult resRule = ruleStrat.analyze(ptrs);
    AlgorithmComparisonItem itemRule;
    itemRule.strategyName = ruleStrat.name();
    itemRule.rootCause = resRule.rootCauseType;
    itemRule.rootEventId = resRule.rootEventId;
    itemRule.confidence = resRule.confidence;
    itemRule.chainsCount = resRule.failureChains.size();
    itemRule.supportingCount = resRule.supportingEventIds.size();
    itemRule.executionTimeMs = resRule.executionTimeMs;
    compResult.comparisons.push_back(itemRule);

    // Run Sliding Time-Window Strategy
    TimeWindowStrategy windowStrat(15);
    AnalysisResult resWindow = windowStrat.analyze(ptrs);
    AlgorithmComparisonItem itemWindow;
    itemWindow.strategyName = windowStrat.name();
    itemWindow.rootCause = resWindow.rootCauseType;
    itemWindow.rootEventId = resWindow.rootEventId;
    itemWindow.confidence = resWindow.confidence;
    itemWindow.chainsCount = resWindow.failureChains.size();
    itemWindow.supportingCount = resWindow.supportingEventIds.size();
    itemWindow.executionTimeMs = resWindow.executionTimeMs;
    compResult.comparisons.push_back(itemWindow);

    std::ostringstream oss;
    if (resRule.rootCauseType == resWindow.rootCauseType) {
        oss << "Both strategies concurred on possible root cause (" << resRule.rootCauseType 
            << "). Rule-based ran in " << std::round(resRule.executionTimeMs * 1000.0) / 1000.0 
            << "ms; Time-window ran in " << std::round(resWindow.executionTimeMs * 1000.0) / 1000.0 << "ms.";
    } else {
        oss << "Strategies reached differing hypotheses: Rule-based identified " << resRule.rootCauseType 
            << " (confidence " << std::round(resRule.confidence) << "%), whereas Time-window identified " 
            << resWindow.rootCauseType << " (confidence " << std::round(resWindow.confidence) << "%).";
    }
    compResult.winnerSummary = oss.str();

    return compResult;
}

} // namespace tracex
