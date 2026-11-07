#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include "third_party/httplib.h"
#include "third_party/json.hpp"
#include "Event.h"
#include "LogParser.h"
#include "Classifier.h"
#include "Analyzer.h"
#include "AnalysisStrategy.h"
#include "Database.h"
#include "Report.h"
#include "Exceptions.h"
#include "Utils.h"

namespace fs = std::filesystem;
using namespace tracex;

void setCorsHeaders(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
}

int main(int argc, char* argv[]) {
    int port = 8080;
    std::string webDir = "frontend";
    std::string dbFile = "data/tracex.db";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--web" && i + 1 < argc) {
            webDir = argv[++i];
        } else if (arg == "--db" && i + 1 < argc) {
            dbFile = argv[++i];
        }
    }

    fs::create_directories("data");
    Database db(dbFile);
    std::cout << "[TraceX Server] Connected to database: " << dbFile << "\n";

    httplib::Server svr;

    // CORS pre-flight handler
    svr.Options(R"(/api/.*)", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        res.status = 200;
    });

    // 1. Health check
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        nlohmann::json j = {{"status", "ok"}, {"engine", "TraceX C++ Log Analysis Engine"}};
        res.set_content(j.dump(), "application/json");
    });

    // 2. List all analysis runs
    svr.Get("/api/runs", [&db](const httplib::Request&, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            auto runs = db.getAllRuns();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& r : runs) {
                arr.push_back(r.toJson());
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        }
    });

    // 3. Upload log file (multipart or raw text)
    svr.Post("/api/upload", [&db](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            std::string content;
            std::string fileName = "uploaded_log.txt";

            if (req.form.has_file("file")) {
                const auto& file = req.form.get_file("file");
                content = file.content;
                if (!file.filename.empty()) {
                    fileName = file.filename;
                }
            } else if (!req.body.empty()) {
                content = req.body;
            } else {
                res.status = 400;
                res.set_content(nlohmann::json({{"error", "No log file provided"}}).dump(), "application/json");
                return;
            }

            auto parser = LogParser::createParser(fileName);
            std::istringstream iss(content);
            auto events = parser->parse(iss);

            if (events.empty() && parser->getSkippedCount() == 0) {
                res.status = 400;
                res.set_content(nlohmann::json({{"error", "Uploaded log file contains no readable events"}}).dump(), "application/json");
                return;
            }

            int runId = db.createAnalysisRun(fileName, "Rule-Based Correlation Strategy");
            for (auto& ev : events) {
                ev->runId = runId;
            }

            // Batch insert events
            db.insertEventsBatch(runId, events);

            // Execute initial analysis
            Analyzer analyzer(std::make_unique<RuleBasedStrategy>());
            AnalysisResult result = analyzer.analyze(events);

            // Save chains
            db.insertChains(runId, result.failureChains);
            db.updateAnalysisRun(runId, result.rootCauseType, result.confidence, result.executionTimeMs);

            nlohmann::json response = {
                {"run_id", runId},
                {"file_name", fileName},
                {"total_events", parser->getParsedCount()},
                {"skipped_lines", parser->getSkippedCount()},
                {"root_cause", result.rootCauseType},
                {"confidence", std::round(result.confidence * 10.0) / 10.0},
                {"explanation", result.explanation},
                {"exec_ms", std::round(result.executionTimeMs * 1000.0) / 1000.0}
            };

            res.set_content(response.dump(), "application/json");

        } catch (const TraceXException& ex) {
            res.status = ex.statusCode();
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        }
    });

    // 4. Run statistics (Totals by level, totals by source, errors per minute)
    svr.Get("/api/stats", [&db](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            int runId = 0;
            if (req.has_param("run_id")) {
                runId = std::stoi(req.get_param_value("run_id"));
            } else {
                auto runs = db.getAllRuns();
                if (!runs.empty()) runId = runs.front().id;
            }

            if (runId == 0) {
                res.set_content(nlohmann::json({
                    {"run_id", 0},
                    {"total_events", 0},
                    {"info_count", 0},
                    {"warn_count", 0},
                    {"error_count", 0},
                    {"fatal_count", 0},
                    {"level_distribution", nlohmann::json::object()},
                    {"source_distribution", nlohmann::json::object()},
                    {"errors_by_source", nlohmann::json::object()},
                    {"errors_per_minute", nlohmann::json::array()}
                }).dump(), "application/json");
                return;
            }

            auto stats = db.getStats(runId);
            res.set_content(stats.dump(), "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        }
    });

    // 5. Paginated and filtered Log Explorer
    svr.Get("/api/logs", [&db](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            int runId = 0;
            if (req.has_param("run_id")) {
                runId = std::stoi(req.get_param_value("run_id"));
            } else {
                auto runs = db.getAllRuns();
                if (!runs.empty()) runId = runs.front().id;
            }

            std::string level = req.get_param_value("level");
            std::string source = req.get_param_value("source");
            std::string query = req.get_param_value("q");
            std::string from = req.get_param_value("from");
            std::string to = req.get_param_value("to");
            int page = req.has_param("page") ? std::stoi(req.get_param_value("page")) : 1;
            int limit = req.has_param("limit") ? std::stoi(req.get_param_value("limit")) : 50;

            auto logs = db.getPaginatedLogs(runId, level, source, query, from, to, page, limit);
            res.set_content(logs.dump(), "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        }
    });

    // 6. Timeline
    svr.Get("/api/timeline", [&db](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            int runId = 0;
            if (req.has_param("run_id")) {
                runId = std::stoi(req.get_param_value("run_id"));
            } else {
                auto runs = db.getAllRuns();
                if (!runs.empty()) runId = runs.front().id;
            }

            auto timeline = db.getTimeline(runId);
            res.set_content(timeline.dump(), "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        }
    });

    // 7. Graph visualization nodes and edges
    svr.Get("/api/graph", [&db](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            int runId = 0;
            if (req.has_param("run_id")) {
                runId = std::stoi(req.get_param_value("run_id"));
            } else {
                auto runs = db.getAllRuns();
                if (!runs.empty()) runId = runs.front().id;
            }

            std::string strategyType = req.has_param("strategy") ? req.get_param_value("strategy") : "rule";
            auto events = db.getEvents(runId);

            Analyzer analyzer;
            if (strategyType == "window") {
                analyzer.setStrategy(std::make_unique<TimeWindowStrategy>(15));
            } else {
                analyzer.setStrategy(std::make_unique<RuleBasedStrategy>());
            }

            auto result = analyzer.analyze(events);
            nlohmann::json resp = {
                {"nodes", nlohmann::json::array()},
                {"edges", nlohmann::json::array()},
                {"root_event_id", result.rootEventId},
                {"root_cause", result.rootCauseType},
                {"confidence", result.confidence}
            };

            for (const auto& n : result.graphNodes) resp["nodes"].push_back(n.toJson());
            for (const auto& e : result.graphEdges) resp["edges"].push_back(e.toJson());

            res.set_content(resp.dump(), "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        }
    });

    // 8. Re-analyze with chosen strategy
    svr.Post("/api/analyze", [&db](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            auto body = nlohmann::json::parse(req.body);
            int runId = body.value("run_id", 0);
            std::string strat = body.value("strategy", "rule");

            auto events = db.getEvents(runId);
            if (events.empty()) {
                res.status = 404;
                res.set_content(nlohmann::json({{"error", "No events found for run_id " + std::to_string(runId)}}).dump(), "application/json");
                return;
            }

            Analyzer analyzer;
            if (strat == "window") {
                analyzer.setStrategy(std::make_unique<TimeWindowStrategy>(15));
            } else {
                analyzer.setStrategy(std::make_unique<RuleBasedStrategy>());
            }

            auto result = analyzer.analyze(events);
            db.updateAnalysisRun(runId, result.rootCauseType, result.confidence, result.executionTimeMs);

            res.set_content(result.toJson().dump(), "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        }
    });

    // 9. What-If Simulation
    svr.Post("/api/whatif", [&db](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            auto body = nlohmann::json::parse(req.body);
            int runId = body.value("run_id", 0);
            std::string strat = body.value("strategy", "rule");
            std::vector<int> removeIds = body.value("remove_event_ids", std::vector<int>{});

            auto events = db.getEvents(runId);
            if (events.empty()) {
                res.status = 404;
                res.set_content(nlohmann::json({{"error", "No events found for run_id " + std::to_string(runId)}}).dump(), "application/json");
                return;
            }

            Analyzer analyzer;
            if (strat == "window") {
                analyzer.setStrategy(std::make_unique<TimeWindowStrategy>(15));
            } else {
                analyzer.setStrategy(std::make_unique<RuleBasedStrategy>());
            }

            auto whatifResult = analyzer.runWhatIf(events, removeIds);
            res.set_content(whatifResult.toJson().dump(), "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        }
    });

    // 10. Algorithm comparison
    svr.Get("/api/compare", [&db](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            int runId = 0;
            if (req.has_param("run_id")) {
                runId = std::stoi(req.get_param_value("run_id"));
            } else {
                auto runs = db.getAllRuns();
                if (!runs.empty()) runId = runs.front().id;
            }

            auto events = db.getEvents(runId);
            if (events.empty()) {
                res.status = 404;
                res.set_content(nlohmann::json({{"error", "No events found for run_id " + std::to_string(runId)}}).dump(), "application/json");
                return;
            }

            Analyzer analyzer;
            auto comp = analyzer.compareAlgorithms(events);
            res.set_content(comp.toJson().dump(), "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        }
    });

    // 11. Full report (JSON or downloadable TXT)
    svr.Get("/api/report", [&db](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            int runId = 0;
            if (req.has_param("run_id")) {
                runId = std::stoi(req.get_param_value("run_id"));
            } else {
                auto runs = db.getAllRuns();
                if (!runs.empty()) runId = runs.front().id;
            }

            std::string format = req.has_param("format") ? req.get_param_value("format") : "json";
            std::string strat = req.has_param("strategy") ? req.get_param_value("strategy") : "rule";

            auto runRec = db.getRun(runId);
            auto events = db.getEvents(runId);

            Analyzer analyzer;
            if (strat == "window") {
                analyzer.setStrategy(std::make_unique<TimeWindowStrategy>(15));
            } else {
                analyzer.setStrategy(std::make_unique<RuleBasedStrategy>());
            }
            auto analysisResult = analyzer.analyze(events);

            Report report(runRec, analysisResult, std::move(events));

            if (format == "txt") {
                res.set_header("Content-Disposition", "attachment; filename=\"tracex_report_run_" + std::to_string(runId) + ".txt\"");
                res.set_content(report.toText(), "text/plain");
            } else {
                res.set_content(report.toJson().dump(2), "application/json");
            }
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        }
    });

    // 12. Live streaming append endpoint
    svr.Post("/api/live/append", [&db](const httplib::Request& req, httplib::Response& res) {
        setCorsHeaders(res);
        try {
            auto body = nlohmann::json::parse(req.body);
            int runId = body.value("run_id", 0);
            if (runId == 0) {
                runId = db.createAnalysisRun("live_stream.log", "Rule-Based Correlation Strategy");
            }

            std::vector<std::string> lines;
            if (body.contains("lines") && body["lines"].is_array()) {
                for (const auto& l : body["lines"]) lines.push_back(l.get<std::string>());
            } else if (body.contains("line")) {
                lines.push_back(body["line"].get<std::string>());
            }

            auto parser = std::make_unique<TextLogParser>(runId);
            auto events = parser->process(lines);
            for (auto& ev : events) ev->runId = runId;

            db.insertEventsBatch(runId, events);

            res.set_content(nlohmann::json({
                {"run_id", runId},
                {"appended_count", events.size()}
            }).dump(), "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
        }
    });

    // Serve static frontend files
    if (fs::exists(webDir)) {
        svr.set_mount_point("/", webDir);
        std::cout << "[TraceX Server] Serving static frontend from: " << webDir << "\n";
    } else {
        std::cout << "[TraceX Server] Note: Web directory " << webDir << " not found, API only mode active.\n";
    }

    std::cout << "================================================================================\n";
    std::cout << "  TRACEX SERVER ACTIVE ON http://localhost:" << port << "\n";
    std::cout << "  Web UI: http://localhost:" << port << "/index.html\n";
    std::cout << "================================================================================\n";

    svr.listen("0.0.0.0", port);
    return 0;
}
