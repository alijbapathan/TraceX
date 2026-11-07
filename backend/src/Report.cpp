#include "Report.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace fs = std::filesystem;

namespace tracex {

Report::Report(RunRecord run, AnalysisResult result, std::vector<std::unique_ptr<Event>> events)
    : runInfo_(std::move(run)), analysisResult_(std::move(result)), events_(std::move(events)) {}

std::string Report::toText() const {
    std::ostringstream oss;
    oss << "================================================================================\n";
    oss << "           TRACEX: SOFTWARE FAILURE INVESTIGATION ENGINE REPORT                 \n";
    oss << "================================================================================\n\n";

    oss << "INVESTIGATION NOTICE:\n";
    oss << "  This report provides an EVIDENCE-BASED HYPOTHESIS regarding the possible root\n";
    oss << "  cause of detected software degradation or failure. It should be evaluated as\n";
    oss << "  a diagnostic recommendation rather than an absolute confirmed cause.\n\n";

    oss << "--------------------------------------------------------------------------------\n";
    oss << "1. ANALYSIS RUN SUMMARY\n";
    oss << "--------------------------------------------------------------------------------\n";
    oss << "  Run ID             : " << runInfo_.id << "\n";
    oss << "  Source File        : " << runInfo_.fileName << "\n";
    oss << "  Strategy Evaluated : " << analysisResult_.strategyName << "\n";
    oss << "  Generated At       : " << Event::formatTimestamp(std::time(nullptr)) << "\n";
    oss << "  Total Events       : " << events_.size() << "\n";
    oss << "  Errors Analyzed    : " << analysisResult_.totalErrorsAnalyzed << "\n";
    oss << "  Execution Time     : " << std::fixed << std::setprecision(3) << analysisResult_.executionTimeMs << " ms\n\n";

    oss << "--------------------------------------------------------------------------------\n";
    oss << "2. HYPOTHESIS & ROOT CAUSE EVALUATION\n";
    oss << "--------------------------------------------------------------------------------\n";
    oss << "  Possible Root Cause: " << analysisResult_.rootCauseType << "\n";
    oss << "  Root Event ID      : #" << analysisResult_.rootEventId << "\n";
    oss << "  Confidence Score   : " << std::fixed << std::setprecision(1) << analysisResult_.confidence << "%\n";
    oss << "  Root Event Details : " << analysisResult_.rootEventSummary << "\n\n";
    oss << "  Detailed Explanation:\n";
    oss << "    " << analysisResult_.explanation << "\n\n";

    oss << "--------------------------------------------------------------------------------\n";
    oss << "3. CORRELATED FAILURE CHAINS (" << analysisResult_.failureChains.size() << " detected)\n";
    oss << "--------------------------------------------------------------------------------\n";
    if (analysisResult_.failureChains.empty()) {
        oss << "  No multi-event failure chains identified.\n\n";
    } else {
        for (size_t i = 0; i < analysisResult_.failureChains.size(); ++i) {
            oss << "  Chain #" << (i + 1) << ": ";
            const auto& chain = analysisResult_.failureChains[i];
            for (size_t j = 0; j < chain.size(); ++j) {
                if (j > 0) oss << " -> ";
                oss << "Event #" << chain[j];
            }
            oss << "\n";
        }
        oss << "\n";
    }

    oss << "--------------------------------------------------------------------------------\n";
    oss << "4. SUPPORTING EVIDENCE (" << analysisResult_.supportingEventIds.size() << " events)\n";
    oss << "--------------------------------------------------------------------------------\n";
    if (analysisResult_.supportingEventIds.empty()) {
        oss << "  No downstream supporting events linked.\n\n";
    } else {
        for (int supId : analysisResult_.supportingEventIds) {
            // Find event
            for (const auto& ev : events_) {
                if (ev && ev->id == supId) {
                    oss << "  - Event #" << ev->id << " [" << ev->timestampString() << "] [" 
                        << Event::levelToString(ev->level) << "] [" << ev->source << "]: " 
                        << ev->message << "\n";
                    break;
                }
            }
        }
        oss << "\n";
    }

    oss << "================================================================================\n";
    oss << "                         END OF TRACEX REPORT                                   \n";
    oss << "================================================================================\n";

    return oss.str();
}

nlohmann::json Report::toJson() const {
    return {
        {"meta", {
            {"report_type", "TraceX Failure Investigation Report"},
            {"disclaimer", "Evidence-based hypothesis only - not a confirmed cause"},
            {"generated_at", Event::formatTimestamp(std::time(nullptr))}
        }},
        {"run", runInfo_.toJson()},
        {"analysis", analysisResult_.toJson()}
    };
}

std::string Report::saveToFile(const std::string& directory, const std::string& format) const {
    fs::create_directories(directory);

    std::time_t now = std::time(nullptr);
    std::string tsStr = std::to_string(now);
    std::string ext = (format == "json") ? ".json" : ".txt";
    std::string filename = "report_run" + std::to_string(runInfo_.id) + "_" + tsStr + ext;
    fs::path fullPath = fs::path(directory) / filename;

    std::ofstream out(fullPath.string());
    if (!out.is_open()) {
        throw FileNotFoundException("Unable to write report to: " + fullPath.string());
    }

    if (format == "json") {
        out << toJson().dump(2);
    } else {
        out << toText();
    }
    out.close();

    return fullPath.string();
}

} // namespace tracex
