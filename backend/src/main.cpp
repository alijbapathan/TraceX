#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "LogParser.h"
#include "Analyzer.h"
#include "AnalysisStrategy.h"
#include "Report.h"
#include "Exceptions.h"

using namespace tracex;

void printUsage() {
    std::cout << "TraceX: Software Failure Investigation Engine (CLI)\n";
    std::cout << "Usage:\n";
    std::cout << "  tracex analyze <logfile> [--strategy rule|window] [--export txt|json]\n";
    std::cout << "  tracex compare <logfile>\n";
    std::cout << "  tracex --help\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string command = argv[1];
    if (command == "--help" || command == "-h") {
        printUsage();
        return 0;
    }

    if (command == "analyze") {
        if (argc < 3) {
            std::cerr << "Error: Missing logfile argument.\n";
            printUsage();
            return 1;
        }

        std::string logFile = argv[2];
        std::string strategyType = "rule";
        std::string exportFormat = "";

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--strategy" && i + 1 < argc) {
                strategyType = argv[++i];
            } else if (arg == "--export" && i + 1 < argc) {
                exportFormat = argv[++i];
            }
        }

        try {
            auto parser = LogParser::createParser(logFile);
            std::cout << "[TraceX] Parsing " << logFile << "...\n";
            auto events = parser->process(logFile);
            std::cout << "[TraceX] Parsed " << parser->getParsedCount() 
                      << " events (" << parser->getSkippedCount() << " skipped lines).\n";

            Analyzer analyzer;
            if (strategyType == "window") {
                analyzer.setStrategy(std::make_unique<TimeWindowStrategy>(15));
            } else {
                analyzer.setStrategy(std::make_unique<RuleBasedStrategy>());
            }

            std::cout << "[TraceX] Executing " << analyzer.getStrategyName() << "...\n";
            AnalysisResult result = analyzer.analyze(events);

            RunRecord fakeRun;
            fakeRun.id = 1;
            fakeRun.fileName = logFile;
            fakeRun.strategy = analyzer.getStrategyName();
            fakeRun.createdAt = Event::formatTimestamp(std::time(nullptr));
            fakeRun.rootCause = result.rootCauseType;
            fakeRun.confidence = result.confidence;
            fakeRun.execMs = result.executionTimeMs;

            Report report(fakeRun, result, std::move(events));
            std::cout << "\n" << report.toText() << "\n";

            if (!exportFormat.empty()) {
                std::string savedPath = report.saveToFile("data", exportFormat);
                std::cout << "[TraceX] Report exported to: " << savedPath << "\n";
            }

        } catch (const TraceXException& ex) {
            std::cerr << "[TraceX Exception] " << ex.what() << "\n";
            return 1;
        } catch (const std::exception& ex) {
            std::cerr << "[Fatal Error] " << ex.what() << "\n";
            return 1;
        }

    } else if (command == "compare") {
        if (argc < 3) {
            std::cerr << "Error: Missing logfile argument.\n";
            printUsage();
            return 1;
        }

        std::string logFile = argv[2];
        try {
            auto parser = LogParser::createParser(logFile);
            auto events = parser->process(logFile);

            Analyzer analyzer;
            auto comparison = analyzer.compareAlgorithms(events);

            std::cout << "================================================================================\n";
            std::cout << "                 TRACEX ALGORITHM COMPARISON BENCHMARK                          \n";
            std::cout << "================================================================================\n";
            std::cout << "Target File: " << logFile << " (" << events.size() << " events)\n\n";

            for (const auto& item : comparison.comparisons) {
                std::cout << "Strategy           : " << item.strategyName << "\n";
                std::cout << "Possible Root Cause: " << item.rootCause << " (Event #" << item.rootEventId << ")\n";
                std::cout << "Confidence Score   : " << item.confidence << "%\n";
                std::cout << "Chains Correlated  : " << item.chainsCount << "\n";
                std::cout << "Supporting Events  : " << item.supportingCount << "\n";
                std::cout << "Execution Time     : " << item.executionTimeMs << " ms\n";
                std::cout << "--------------------------------------------------------------------------------\n";
            }

            std::cout << "\nBenchmark Summary: " << comparison.winnerSummary << "\n";
            std::cout << "================================================================================\n";

        } catch (const std::exception& ex) {
            std::cerr << "[Error] " << ex.what() << "\n";
            return 1;
        }

    } else {
        std::cerr << "Unknown command: " << command << "\n";
        printUsage();
        return 1;
    }

    return 0;
}
