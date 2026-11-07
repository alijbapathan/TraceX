#pragma once

#include <string>
#include <vector>
#include <memory>
#include "Event.h"
#include "Exceptions.h"
#include "third_party/json.hpp"

// Forward declaration of sqlite3 struct
struct sqlite3;

namespace tracex {

struct RunRecord {
    int id = 0;
    std::string fileName;
    std::string strategy;
    std::string createdAt;
    std::string rootCause;
    double confidence = 0.0;
    double execMs = 0.0;
    int totalEvents = 0;
    int errorCount = 0;

    nlohmann::json toJson() const {
        return {
            {"id", id},
            {"file_name", fileName},
            {"strategy", strategy},
            {"created_at", createdAt},
            {"root_cause", rootCause},
            {"confidence", std::round(confidence * 10.0) / 10.0},
            {"exec_ms", std::round(execMs * 1000.0) / 1000.0},
            {"total_events", totalEvents},
            {"error_count", errorCount}
        };
    }
};

class Database {
private:
    sqlite3* db_ = nullptr;
    std::string dbPath_;

public:
    Database();
    explicit Database(const std::string& dbPath);
    ~Database();

    // Disable copy, enable move (RAII)
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    void open(const std::string& dbPath);
    void close();
    bool isOpen() const noexcept { return db_ != nullptr; }

    void initializeSchema();

    int createAnalysisRun(const std::string& fileName, const std::string& strategy);
    void updateAnalysisRun(int runId, const std::string& rootCause, double confidence, double execMs);
    std::vector<RunRecord> getAllRuns();
    RunRecord getRun(int runId);

    void insertEventsBatch(int runId, const std::vector<std::unique_ptr<Event>>& events);
    int insertSingleEvent(int runId, const Event& ev);
    void insertChains(int runId, const std::vector<std::vector<int>>& chains);

    std::vector<std::unique_ptr<Event>> getEvents(int runId);
    nlohmann::json getStats(int runId);
    nlohmann::json getPaginatedLogs(int runId, const std::string& level, const std::string& source,
                                    const std::string& query, const std::string& from, const std::string& to,
                                    int page, int limit);
    nlohmann::json getTimeline(int runId);

private:
    void executeSql(const std::string& sql);
};

} // namespace tracex
