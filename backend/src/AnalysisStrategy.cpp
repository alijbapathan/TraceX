#include "AnalysisStrategy.h"
#include "Utils.h"
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace tracex {

nlohmann::json AnalysisResult::toJson() const {
    nlohmann::json nodesJson = nlohmann::json::array();
    for (const auto& node : graphNodes) {
        nodesJson.push_back(node.toJson());
    }

    nlohmann::json edgesJson = nlohmann::json::array();
    for (const auto& edge : graphEdges) {
        edgesJson.push_back(edge.toJson());
    }

    nlohmann::json chainsJson = nlohmann::json::array();
    for (const auto& chain : failureChains) {
        chainsJson.push_back(chain);
    }

    return {
        {"strategy", strategyName},
        {"root_cause_type", rootCauseType},
        {"root_event_id", rootEventId},
        {"root_event_summary", rootEventSummary},
        {"confidence", std::round(confidence * 10.0) / 10.0},
        {"explanation", explanation},
        {"supporting_event_ids", supportingEventIds},
        {"chains", chainsJson},
        {"nodes", nodesJson},
        {"edges", edgesJson},
        {"exec_ms", std::round(executionTimeMs * 1000.0) / 1000.0},
        {"total_errors_analyzed", totalErrorsAnalyzed}
    };
}

// ---------------- RuleBasedStrategy ----------------

AnalysisResult RuleBasedStrategy::analyze(const std::vector<Event*>& sortedEvents) {
    auto startTime = std::chrono::high_resolution_clock::now();
    AnalysisResult result;
    result.strategyName = name();

    // Filter to only error/fatal events using template utility
    std::vector<Event*> errorEvents = filterBy(sortedEvents, [](Event* e) {
        return e != nullptr && e->isError();
    });

    result.totalErrorsAnalyzed = errorEvents.size();
    if (errorEvents.empty()) {
        result.rootCauseType = "NONE";
        result.explanation = "No error events detected in log sequence. System operating nominally.";
        result.confidence = 0.0;
        auto endTime = std::chrono::high_resolution_clock::now();
        result.executionTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        return result;
    }

    // Candidate structure
    struct Candidate {
        Event* rootEvent = nullptr;
        std::string ruleName;
        std::vector<Event*> supportingEvents;
        double temporalPrecedence = 1.0;
        double downstreamRatio = 0.0;
        double ruleMatch = 1.0;
        double confidence = 0.0;
    };

    std::vector<Candidate> candidates;

    for (size_t i = 0; i < errorEvents.size(); ++i) {
        Event* candidate = errorEvents[i];
        time_t cTime = candidate->timestamp;

        // Rule 1: DB_FAILURE followed by >= 2 API_FAILURE within 10s
        if (candidate->type == EventType::DB_FAILURE) {
            std::vector<Event*> downstream;
            for (size_t j = i + 1; j < errorEvents.size(); ++j) {
                if (errorEvents[j]->timestamp - cTime <= 10) {
                    if (errorEvents[j]->type == EventType::API_FAILURE || errorEvents[j]->source == "API") {
                        downstream.push_back(errorEvents[j]);
                    }
                } else {
                    break;
                }
            }
            if (downstream.size() >= 2) {
                Candidate c;
                c.rootEvent = candidate;
                c.ruleName = "Database Cascade Rule (DB_FAILURE followed by >= 2 API failures within 10s)";
                c.supportingEvents = downstream;
                c.temporalPrecedence = 1.0; // Earliest error of the chain
                size_t totalInWindow = 1 + downstream.size();
                c.downstreamRatio = std::min(1.0, static_cast<double>(downstream.size()) / static_cast<double>(totalInWindow));
                c.ruleMatch = 1.0;
                c.confidence = calculateConfidence(c.temporalPrecedence, c.downstreamRatio, c.ruleMatch);
                candidates.push_back(c);
            }
        }

        // Rule 2: NETWORK_FAILURE followed by DB or API failures within 10s
        if (candidate->type == EventType::NETWORK_FAILURE) {
            std::vector<Event*> downstream;
            for (size_t j = i + 1; j < errorEvents.size(); ++j) {
                if (errorEvents[j]->timestamp - cTime <= 10) {
                    if (errorEvents[j]->type == EventType::DB_FAILURE || errorEvents[j]->type == EventType::API_FAILURE ||
                        errorEvents[j]->source == "DATABASE" || errorEvents[j]->source == "API") {
                        downstream.push_back(errorEvents[j]);
                    }
                } else {
                    break;
                }
            }
            if (!downstream.empty()) {
                Candidate c;
                c.rootEvent = candidate;
                c.ruleName = "Network Outage Cascade Rule (NETWORK_FAILURE triggering DB/API disconnects within 10s)";
                c.supportingEvents = downstream;
                c.temporalPrecedence = 1.0;
                size_t totalInWindow = 1 + downstream.size();
                c.downstreamRatio = std::min(1.0, static_cast<double>(downstream.size()) / static_cast<double>(totalInWindow));
                c.ruleMatch = 1.0;
                c.confidence = calculateConfidence(c.temporalPrecedence, c.downstreamRatio, c.ruleMatch);
                candidates.push_back(c);
            }
        }

        // Rule 3: MEMORY_PRESSURE followed by timeouts/crashes within 15s
        if (candidate->type == EventType::MEMORY_PRESSURE) {
            std::vector<Event*> downstream;
            for (size_t j = i + 1; j < errorEvents.size(); ++j) {
                if (errorEvents[j]->timestamp - cTime <= 15) {
                    std::string m = toLower(errorEvents[j]->message);
                    if (errorEvents[j]->type == EventType::API_FAILURE || errorEvents[j]->type == EventType::DB_FAILURE ||
                        m.find("timeout") != std::string::npos || m.find("crash") != std::string::npos ||
                        m.find("outofmemory") != std::string::npos || m.find("sigabrt") != std::string::npos) {
                        downstream.push_back(errorEvents[j]);
                    }
                } else {
                    break;
                }
            }
            if (!downstream.empty()) {
                Candidate c;
                c.rootEvent = candidate;
                c.ruleName = "Memory Pressure Cascade Rule (MEMORY_PRESSURE followed by downstream resource exhaustion within 15s)";
                c.supportingEvents = downstream;
                c.temporalPrecedence = 1.0;
                size_t totalInWindow = 1 + downstream.size();
                c.downstreamRatio = std::min(1.0, static_cast<double>(downstream.size()) / static_cast<double>(totalInWindow));
                c.ruleMatch = 1.0;
                c.confidence = calculateConfidence(c.temporalPrecedence, c.downstreamRatio, c.ruleMatch);
                candidates.push_back(c);
            }
        }

        // Rule 4: AUTH_FAILURE followed by API 401/403 failures within 10s
        if (candidate->type == EventType::AUTH_FAILURE) {
            std::vector<Event*> downstream;
            for (size_t j = i + 1; j < errorEvents.size(); ++j) {
                if (errorEvents[j]->timestamp - cTime <= 10) {
                    std::string m = toLower(errorEvents[j]->message);
                    if (errorEvents[j]->source == "AUTH" || errorEvents[j]->source == "API" ||
                        m.find("401") != std::string::npos || m.find("403") != std::string::npos ||
                        m.find("unauthorized") != std::string::npos || m.find("forbidden") != std::string::npos) {
                        downstream.push_back(errorEvents[j]);
                    }
                } else {
                    break;
                }
            }
            if (!downstream.empty()) {
                Candidate c;
                c.rootEvent = candidate;
                c.ruleName = "Authentication Cascade Rule (AUTH_FAILURE followed by repeated 401/403 failures within 10s)";
                c.supportingEvents = downstream;
                c.temporalPrecedence = 1.0;
                size_t totalInWindow = 1 + downstream.size();
                c.downstreamRatio = std::min(1.0, static_cast<double>(downstream.size()) / static_cast<double>(totalInWindow));
                c.ruleMatch = 1.0;
                c.confidence = calculateConfidence(c.temporalPrecedence, c.downstreamRatio, c.ruleMatch);
                candidates.push_back(c);
            }
        }
    }

    // Select the best candidate (highest confidence or earliest)
    if (!candidates.empty()) {
        sortBy(candidates, [](const Candidate& a, const Candidate& b) {
            if (std::abs(a.confidence - b.confidence) > 0.01) {
                return a.confidence > b.confidence;
            }
            return a.rootEvent->timestamp < b.rootEvent->timestamp;
        });

        const auto& best = candidates.front();
        result.rootEventId = best.rootEvent->id;
        result.rootCauseType = Event::typeToString(best.rootEvent->type);
        result.rootEventSummary = best.rootEvent->timestampString() + " [" + best.rootEvent->source + "] " + best.rootEvent->message;
        result.confidence = best.confidence;
        
        std::ostringstream oss;
        oss << "Possible root cause: " << result.rootCauseType << " at event #" << best.rootEvent->id 
            << " (evidence-based hypothesis). Matched " << best.ruleName << " with " 
            << best.supportingEvents.size() << " downstream cascade events.";
        result.explanation = oss.str();

        std::vector<int> chain;
        chain.push_back(best.rootEvent->id);

        GraphNode rootNode;
        rootNode.id = best.rootEvent->id;
        rootNode.label = "#" + std::to_string(best.rootEvent->id) + " " + best.rootEvent->source;
        rootNode.level = Event::levelToString(best.rootEvent->level);
        rootNode.source = best.rootEvent->source;
        rootNode.type = Event::typeToString(best.rootEvent->type);
        rootNode.timestamp = best.rootEvent->timestampString();
        rootNode.isRoot = true;
        rootNode.isSupporting = false;
        result.graphNodes.push_back(rootNode);

        for (const auto& sup : best.supportingEvents) {
            result.supportingEventIds.push_back(sup->id);
            chain.push_back(sup->id);

            GraphNode supNode;
            supNode.id = sup->id;
            supNode.label = "#" + std::to_string(sup->id) + " " + sup->source;
            supNode.level = Event::levelToString(sup->level);
            supNode.source = sup->source;
            supNode.type = Event::typeToString(sup->type);
            supNode.timestamp = sup->timestampString();
            supNode.isRoot = false;
            supNode.isSupporting = true;
            result.graphNodes.push_back(supNode);

            GraphEdge edge;
            edge.from = best.rootEvent->id;
            edge.to = sup->id;
            edge.label = "cascaded to";
            edge.relation = "likely caused";
            result.graphEdges.push_back(edge);
        }
        result.failureChains.push_back(chain);

    } else {
        // Fallback: earliest error as candidate
        Event* earliest = errorEvents.front();
        result.rootEventId = earliest->id;
        result.rootCauseType = Event::typeToString(earliest->type);
        result.rootEventSummary = earliest->timestampString() + " [" + earliest->source + "] " + earliest->message;
        result.confidence = calculateConfidence(1.0, 0.2, 0.5); // Fallback: rule match 0.5
        
        std::ostringstream oss;
        oss << "Possible root cause: " << result.rootCauseType << " at event #" << earliest->id
            << " (evidence-based hypothesis). Earliest chronologically observed error; no complex cascade rule triggered.";
        result.explanation = oss.str();

        std::vector<int> chain;
        chain.push_back(earliest->id);
        result.failureChains.push_back(chain);

        GraphNode node;
        node.id = earliest->id;
        node.label = "#" + std::to_string(earliest->id) + " " + earliest->source;
        node.level = Event::levelToString(earliest->level);
        node.source = earliest->source;
        node.type = Event::typeToString(earliest->type);
        node.timestamp = earliest->timestampString();
        node.isRoot = true;
        node.isSupporting = false;
        result.graphNodes.push_back(node);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.executionTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    return result;
}

// ---------------- TimeWindowStrategy ----------------

AnalysisResult TimeWindowStrategy::analyze(const std::vector<Event*>& sortedEvents) {
    auto startTime = std::chrono::high_resolution_clock::now();
    AnalysisResult result;
    result.strategyName = name();

    std::vector<Event*> errorEvents = filterBy(sortedEvents, [](Event* e) {
        return e != nullptr && e->isError();
    });

    result.totalErrorsAnalyzed = errorEvents.size();
    if (errorEvents.empty()) {
        result.rootCauseType = "NONE";
        result.explanation = "No error events detected in log sequence. System operating nominally.";
        result.confidence = 0.0;
        auto endTime = std::chrono::high_resolution_clock::now();
        result.executionTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        return result;
    }

    struct WindowResult {
        Event* candidate = nullptr;
        std::vector<Event*> inWindowErrors;
        double downstreamRatio = 0.0;
        double confidence = 0.0;
    };

    std::vector<WindowResult> windows;

    // Slide temporal window over error events
    for (size_t i = 0; i < errorEvents.size(); ++i) {
        Event* current = errorEvents[i];
        time_t wStart = current->timestamp;
        time_t wEnd = wStart + windowSeconds_;

        std::vector<Event*> inWindow;
        for (size_t j = i; j < errorEvents.size(); ++j) {
            if (errorEvents[j]->timestamp <= wEnd) {
                inWindow.push_back(errorEvents[j]);
            } else {
                break;
            }
        }

        if (inWindow.size() >= 3) {
            WindowResult wr;
            wr.candidate = inWindow.front();
            wr.inWindowErrors = inWindow;
            double downstreamCount = static_cast<double>(inWindow.size() - 1);
            wr.downstreamRatio = std::min(1.0, downstreamCount / static_cast<double>(inWindow.size()));
            double temporalPrec = 1.0;
            // Rule match is 0.5 for window-only evidence as specified
            wr.confidence = calculateConfidence(temporalPrec, wr.downstreamRatio, 0.5);
            windows.push_back(wr);
        }
    }

    if (!windows.empty()) {
        sortBy(windows, [](const WindowResult& a, const WindowResult& b) {
            if (std::abs(a.confidence - b.confidence) > 0.01) {
                return a.confidence > b.confidence;
            }
            return a.inWindowErrors.size() > b.inWindowErrors.size();
        });

        const auto& best = windows.front();
        result.rootEventId = best.candidate->id;
        result.rootCauseType = Event::typeToString(best.candidate->type);
        result.rootEventSummary = best.candidate->timestampString() + " [" + best.candidate->source + "] " + best.candidate->message;
        result.confidence = best.confidence;

        std::ostringstream oss;
        oss << "Possible root cause: " << result.rootCauseType << " at event #" << best.candidate->id
            << " (evidence-based hypothesis). Identified in sliding " << windowSeconds_ 
            << "s window containing " << best.inWindowErrors.size() << " clustered error events.";
        result.explanation = oss.str();

        std::vector<int> chain;
        chain.push_back(best.candidate->id);

        GraphNode rootNode;
        rootNode.id = best.candidate->id;
        rootNode.label = "#" + std::to_string(best.candidate->id) + " " + best.candidate->source;
        rootNode.level = Event::levelToString(best.candidate->level);
        rootNode.source = best.candidate->source;
        rootNode.type = Event::typeToString(best.candidate->type);
        rootNode.timestamp = best.candidate->timestampString();
        rootNode.isRoot = true;
        rootNode.isSupporting = false;
        result.graphNodes.push_back(rootNode);

        for (size_t k = 1; k < best.inWindowErrors.size(); ++k) {
            Event* sup = best.inWindowErrors[k];
            result.supportingEventIds.push_back(sup->id);
            chain.push_back(sup->id);

            GraphNode supNode;
            supNode.id = sup->id;
            supNode.label = "#" + std::to_string(sup->id) + " " + sup->source;
            supNode.level = Event::levelToString(sup->level);
            supNode.source = sup->source;
            supNode.type = Event::typeToString(sup->type);
            supNode.timestamp = sup->timestampString();
            supNode.isRoot = false;
            supNode.isSupporting = true;
            result.graphNodes.push_back(supNode);

            GraphEdge edge;
            edge.from = best.candidate->id;
            edge.to = sup->id;
            edge.label = "correlated in window";
            edge.relation = "temporal correlation";
            result.graphEdges.push_back(edge);
        }
        result.failureChains.push_back(chain);

    } else {
        Event* earliest = errorEvents.front();
        result.rootEventId = earliest->id;
        result.rootCauseType = Event::typeToString(earliest->type);
        result.rootEventSummary = earliest->timestampString() + " [" + earliest->source + "] " + earliest->message;
        result.confidence = calculateConfidence(1.0, 0.1, 0.5);

        std::ostringstream oss;
        oss << "Possible root cause: " << result.rootCauseType << " at event #" << earliest->id
            << " (evidence-based hypothesis). Earliest chronologically observed error; insufficient errors for cluster window.";
        result.explanation = oss.str();

        std::vector<int> chain;
        chain.push_back(earliest->id);
        result.failureChains.push_back(chain);

        GraphNode node;
        node.id = earliest->id;
        node.label = "#" + std::to_string(earliest->id) + " " + earliest->source;
        node.level = Event::levelToString(earliest->level);
        node.source = earliest->source;
        node.type = Event::typeToString(earliest->type);
        node.timestamp = earliest->timestampString();
        node.isRoot = true;
        node.isSupporting = false;
        result.graphNodes.push_back(node);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.executionTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    return result;
}

} // namespace tracex
