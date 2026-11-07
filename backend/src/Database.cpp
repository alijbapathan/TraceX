#include "Database.h"
#include "third_party/sqlite3.h"
#include "Utils.h"
#include <iostream>
#include <sstream>

namespace tracex {

Database::Database() = default;

Database::Database(const std::string& dbPath) {
    open(dbPath);
}

Database::~Database() {
    close();
}

Database::Database(Database&& other) noexcept
    : db_(other.db_), dbPath_(std::move(other.dbPath_)) {
    other.db_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        dbPath_ = std::move(other.dbPath_);
        other.db_ = nullptr;
    }
    return *this;
}

void Database::open(const std::string& dbPath) {
    close();
    dbPath_ = dbPath;
    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "Failed to open sqlite3 database";
        close();
        throw DatabaseException(err + " [path=" + dbPath + "]");
    }
    initializeSchema();
}

void Database::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::executeSql(const std::string& sql) {
    if (!db_) throw DatabaseException("Database not open");
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string err = errMsg ? errMsg : "Unknown SQL error";
        sqlite3_free(errMsg);
        throw DatabaseException(err + " [SQL: " + sql + "]");
    }
}

void Database::initializeSchema() {
    executeSql("PRAGMA journal_mode = WAL;");
    executeSql("PRAGMA synchronous = NORMAL;");

    const std::string schema = R"(
        CREATE TABLE IF NOT EXISTS analysis_runs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_name TEXT,
            strategy TEXT,
            created_at TEXT,
            root_cause TEXT,
            confidence REAL,
            exec_ms REAL
        );

        CREATE TABLE IF NOT EXISTS events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            run_id INTEGER,
            ts TEXT,
            level TEXT,
            source TEXT,
            type TEXT,
            message TEXT,
            FOREIGN KEY(run_id) REFERENCES analysis_runs(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS chains (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            run_id INTEGER,
            event_ids TEXT,
            FOREIGN KEY(run_id) REFERENCES analysis_runs(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_events_run ON events(run_id);
        CREATE INDEX IF NOT EXISTS idx_events_level ON events(run_id, level);
        CREATE INDEX IF NOT EXISTS idx_events_ts ON events(run_id, ts);
        CREATE INDEX IF NOT EXISTS idx_chains_run ON chains(run_id);
    )";
    executeSql(schema);
}

int Database::createAnalysisRun(const std::string& fileName, const std::string& strategy) {
    if (!db_) throw DatabaseException("Database not open");
    
    std::string nowStr = Event::formatTimestamp(std::time(nullptr));
    const char* sql = "INSERT INTO analysis_runs (file_name, strategy, created_at, root_cause, confidence, exec_ms) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseException(sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, fileName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, strategy.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, nowStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, "PENDING", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, 0.0);
    sqlite3_bind_double(stmt, 6, 0.0);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw DatabaseException(err);
    }
    sqlite3_finalize(stmt);

    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

void Database::updateAnalysisRun(int runId, const std::string& rootCause, double confidence, double execMs) {
    if (!db_) throw DatabaseException("Database not open");
    const char* sql = "UPDATE analysis_runs SET root_cause = ?, confidence = ?, exec_ms = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseException(sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, rootCause.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, confidence);
    sqlite3_bind_double(stmt, 3, execMs);
    sqlite3_bind_int(stmt, 4, runId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw DatabaseException(err);
    }
    sqlite3_finalize(stmt);
}

std::vector<RunRecord> Database::getAllRuns() {
    std::vector<RunRecord> runs;
    if (!db_) return runs;

    const char* sql = R"(
        SELECT r.id, r.file_name, r.strategy, r.created_at, r.root_cause, r.confidence, r.exec_ms,
               COUNT(e.id) AS total_events,
               SUM(CASE WHEN e.level IN ('ERROR', 'FATAL') THEN 1 ELSE 0 END) AS error_count
        FROM analysis_runs r
        LEFT JOIN events e ON r.id = e.run_id
        GROUP BY r.id
        ORDER BY r.id DESC;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseException(sqlite3_errmsg(db_));
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RunRecord rec;
        rec.id = sqlite3_column_int(stmt, 0);
        rec.fileName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.strategy = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        rec.createdAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* rcText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        rec.rootCause = rcText ? rcText : "UNKNOWN";
        rec.confidence = sqlite3_column_double(stmt, 5);
        rec.execMs = sqlite3_column_double(stmt, 6);
        rec.totalEvents = sqlite3_column_int(stmt, 7);
        rec.errorCount = sqlite3_column_int(stmt, 8);
        runs.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return runs;
}

RunRecord Database::getRun(int runId) {
    if (!db_) throw DatabaseException("Database not open");
    const char* sql = R"(
        SELECT r.id, r.file_name, r.strategy, r.created_at, r.root_cause, r.confidence, r.exec_ms,
               COUNT(e.id) AS total_events,
               SUM(CASE WHEN e.level IN ('ERROR', 'FATAL') THEN 1 ELSE 0 END) AS error_count
        FROM analysis_runs r
        LEFT JOIN events e ON r.id = e.run_id
        WHERE r.id = ?
        GROUP BY r.id;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseException(sqlite3_errmsg(db_));
    }
    sqlite3_bind_int(stmt, 1, runId);

    RunRecord rec;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        rec.id = sqlite3_column_int(stmt, 0);
        rec.fileName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rec.strategy = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        rec.createdAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* rcText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        rec.rootCause = rcText ? rcText : "UNKNOWN";
        rec.confidence = sqlite3_column_double(stmt, 5);
        rec.execMs = sqlite3_column_double(stmt, 6);
        rec.totalEvents = sqlite3_column_int(stmt, 7);
        rec.errorCount = sqlite3_column_int(stmt, 8);
    } else {
        sqlite3_finalize(stmt);
        throw FileNotFoundException("Run ID " + std::to_string(runId));
    }
    sqlite3_finalize(stmt);
    return rec;
}

void Database::insertEventsBatch(int runId, const std::vector<std::unique_ptr<Event>>& events) {
    if (!db_) throw DatabaseException("Database not open");
    if (events.empty()) return;

    executeSql("BEGIN TRANSACTION;");

    const char* sql = "INSERT INTO events (run_id, ts, level, source, type, message) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        executeSql("ROLLBACK;");
        throw DatabaseException(sqlite3_errmsg(db_));
    }

    for (const auto& ev : events) {
        if (!ev) continue;
        sqlite3_bind_int(stmt, 1, runId);
        std::string ts = ev->timestampString();
        sqlite3_bind_text(stmt, 2, ts.c_str(), -1, SQLITE_TRANSIENT);
        std::string lvl = Event::levelToString(ev->level);
        sqlite3_bind_text(stmt, 3, lvl.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, ev->source.c_str(), -1, SQLITE_TRANSIENT);
        std::string type = Event::typeToString(ev->type);
        sqlite3_bind_text(stmt, 5, type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, ev->message.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            executeSql("ROLLBACK;");
            throw DatabaseException(err);
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    executeSql("COMMIT;");
}

int Database::insertSingleEvent(int runId, const Event& ev) {
    if (!db_) throw DatabaseException("Database not open");
    const char* sql = "INSERT INTO events (run_id, ts, level, source, type, message) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseException(sqlite3_errmsg(db_));
    }

    sqlite3_bind_int(stmt, 1, runId);
    std::string ts = ev.timestampString();
    sqlite3_bind_text(stmt, 2, ts.c_str(), -1, SQLITE_TRANSIENT);
    std::string lvl = Event::levelToString(ev.level);
    sqlite3_bind_text(stmt, 3, lvl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, ev.source.c_str(), -1, SQLITE_TRANSIENT);
    std::string type = Event::typeToString(ev.type);
    sqlite3_bind_text(stmt, 5, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, ev.message.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw DatabaseException(err);
    }
    sqlite3_finalize(stmt);
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

void Database::insertChains(int runId, const std::vector<std::vector<int>>& chains) {
    if (!db_ || chains.empty()) return;
    executeSql("BEGIN TRANSACTION;");

    const char* sql = "INSERT INTO chains (run_id, event_ids) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        executeSql("ROLLBACK;");
        throw DatabaseException(sqlite3_errmsg(db_));
    }

    for (const auto& chain : chains) {
        std::ostringstream oss;
        for (size_t i = 0; i < chain.size(); ++i) {
            if (i > 0) oss << ",";
            oss << chain[i];
        }
        std::string chainStr = oss.str();

        sqlite3_bind_int(stmt, 1, runId);
        sqlite3_bind_text(stmt, 2, chainStr.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            executeSql("ROLLBACK;");
            throw DatabaseException(err);
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    executeSql("COMMIT;");
}

std::vector<std::unique_ptr<Event>> Database::getEvents(int runId) {
    std::vector<std::unique_ptr<Event>> events;
    if (!db_) return events;

    std::string sql = "SELECT id, run_id, ts, level, source, type, message FROM events WHERE run_id = ? ORDER BY id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseException(sqlite3_errmsg(db_));
    }
    sqlite3_bind_int(stmt, 1, runId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        int rId = sqlite3_column_int(stmt, 1);
        std::string tsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        std::string lvlStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        std::string srcStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        std::string typStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        std::string msgStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));

        time_t ts = Event::parseTimestamp(tsStr);
        Level lvl = Event::stringToLevel(lvlStr);
        EventType typ = Event::stringToType(typStr);

        events.push_back(std::make_unique<Event>(id, rId, ts, lvl, srcStr, msgStr, typ));
    }
    sqlite3_finalize(stmt);
    return events;
}

nlohmann::json Database::getStats(int runId) {
    if (!db_) throw DatabaseException("Database not open");

    // 1. Level counts
    std::map<std::string, int> levelCounts;
    std::string sqlLevel = "SELECT level, COUNT(*) FROM events WHERE run_id = ? GROUP BY level;";
    sqlite3_stmt* stmtLevel = nullptr;
    if (sqlite3_prepare_v2(db_, sqlLevel.c_str(), -1, &stmtLevel, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmtLevel, 1, runId);
        while (sqlite3_step(stmtLevel) == SQLITE_ROW) {
            std::string lvl = reinterpret_cast<const char*>(sqlite3_column_text(stmtLevel, 0));
            int cnt = sqlite3_column_int(stmtLevel, 1);
            levelCounts[lvl] = cnt;
        }
        sqlite3_finalize(stmtLevel);
    }

    // 2. Source counts
    std::map<std::string, int> sourceCounts;
    std::string sqlSource = "SELECT source, COUNT(*) FROM events WHERE run_id = ? GROUP BY source;";
    sqlite3_stmt* stmtSource = nullptr;
    if (sqlite3_prepare_v2(db_, sqlSource.c_str(), -1, &stmtSource, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmtSource, 1, runId);
        while (sqlite3_step(stmtSource) == SQLITE_ROW) {
            std::string src = reinterpret_cast<const char*>(sqlite3_column_text(stmtSource, 0));
            int cnt = sqlite3_column_int(stmtSource, 1);
            sourceCounts[src] = cnt;
        }
        sqlite3_finalize(stmtSource);
    }

    // 3. Errors by source
    std::map<std::string, int> errorsBySource;
    std::string sqlErrSource = "SELECT source, COUNT(*) FROM events WHERE run_id = ? AND level IN ('ERROR', 'FATAL') GROUP BY source;";
    sqlite3_stmt* stmtErrSource = nullptr;
    if (sqlite3_prepare_v2(db_, sqlErrSource.c_str(), -1, &stmtErrSource, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmtErrSource, 1, runId);
        while (sqlite3_step(stmtErrSource) == SQLITE_ROW) {
            std::string src = reinterpret_cast<const char*>(sqlite3_column_text(stmtErrSource, 0));
            int cnt = sqlite3_column_int(stmtErrSource, 1);
            errorsBySource[src] = cnt;
        }
        sqlite3_finalize(stmtErrSource);
    }

    // 4. Errors per minute timeline (for Chart.js line graph)
    std::vector<std::pair<std::string, int>> errorsPerMinute;
    std::string sqlMinute = "SELECT substr(ts, 1, 16) AS minute, COUNT(*) FROM events WHERE run_id = ? AND level IN ('ERROR', 'FATAL') GROUP BY minute ORDER BY minute ASC;";
    sqlite3_stmt* stmtMinute = nullptr;
    if (sqlite3_prepare_v2(db_, sqlMinute.c_str(), -1, &stmtMinute, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmtMinute, 1, runId);
        while (sqlite3_step(stmtMinute) == SQLITE_ROW) {
            std::string min = reinterpret_cast<const char*>(sqlite3_column_text(stmtMinute, 0));
            int cnt = sqlite3_column_int(stmtMinute, 1);
            errorsPerMinute.push_back({min, cnt});
        }
        sqlite3_finalize(stmtMinute);
    }

    int total = 0;
    for (const auto& pair : levelCounts) total += pair.second;

    nlohmann::json epmJson = nlohmann::json::array();
    for (const auto& item : errorsPerMinute) {
        epmJson.push_back({{"time", item.first}, {"count", item.second}});
    }

    return {
        {"run_id", runId},
        {"total_events", total},
        {"info_count", levelCounts["INFO"]},
        {"warn_count", levelCounts["WARN"]},
        {"error_count", levelCounts["ERROR"] + levelCounts["FATAL"]},
        {"fatal_count", levelCounts["FATAL"]},
        {"level_distribution", levelCounts},
        {"source_distribution", sourceCounts},
        {"errors_by_source", errorsBySource},
        {"errors_per_minute", epmJson}
    };
}

nlohmann::json Database::getPaginatedLogs(int runId, const std::string& level, const std::string& source,
                                          const std::string& query, const std::string& from, const std::string& to,
                                          int page, int limit) {
    if (!db_) throw DatabaseException("Database not open");
    if (page < 1) page = 1;
    if (limit < 1) limit = 50;

    std::string where = "WHERE run_id = " + std::to_string(runId);
    if (!level.empty()) {
        where += " AND level = '" + level + "'";
    }
    if (!source.empty()) {
        where += " AND source = '" + source + "'";
    }
    if (!from.empty()) {
        where += " AND ts >= '" + from + "'";
    }
    if (!to.empty()) {
        where += " AND ts <= '" + to + "'";
    }
    if (!query.empty()) {
        where += " AND (message LIKE '%" + query + "%' OR source LIKE '%" + query + "%')";
    }

    // Total matching count
    std::string countSql = "SELECT COUNT(*) FROM events " + where + ";";
    sqlite3_stmt* countStmt = nullptr;
    int totalCount = 0;
    if (sqlite3_prepare_v2(db_, countSql.c_str(), -1, &countStmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(countStmt) == SQLITE_ROW) {
            totalCount = sqlite3_column_int(countStmt, 0);
        }
        sqlite3_finalize(countStmt);
    }

    int offset = (page - 1) * limit;
    int totalPages = (totalCount + limit - 1) / limit;
    if (totalPages == 0) totalPages = 1;

    std::string querySql = "SELECT id, run_id, ts, level, source, type, message FROM events " + where +
                           " ORDER BY id ASC LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset) + ";";

    sqlite3_stmt* stmt = nullptr;
    nlohmann::json eventsJson = nlohmann::json::array();
    if (sqlite3_prepare_v2(db_, querySql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            eventsJson.push_back({
                {"id", sqlite3_column_int(stmt, 0)},
                {"run_id", sqlite3_column_int(stmt, 1)},
                {"ts", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))},
                {"level", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))},
                {"source", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))},
                {"type", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))},
                {"message", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6))}
            });
        }
        sqlite3_finalize(stmt);
    }

    return {
        {"events", eventsJson},
        {"total", totalCount},
        {"page", page},
        {"limit", limit},
        {"total_pages", totalPages}
    };
}

nlohmann::json Database::getTimeline(int runId) {
    if (!db_) throw DatabaseException("Database not open");

    // Retrieve active chains for this run
    std::set<int> inChainEventIds;
    std::string chainSql = "SELECT event_ids FROM chains WHERE run_id = ?;";
    sqlite3_stmt* cStmt = nullptr;
    if (sqlite3_prepare_v2(db_, chainSql.c_str(), -1, &cStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(cStmt, 1, runId);
        while (sqlite3_step(cStmt) == SQLITE_ROW) {
            std::string cStr = reinterpret_cast<const char*>(sqlite3_column_text(cStmt, 0));
            auto tokens = split(cStr, ',');
            for (const auto& tok : tokens) {
                try {
                    inChainEventIds.insert(std::stoi(tok));
                } catch (...) {}
            }
        }
        sqlite3_finalize(cStmt);
    }

    std::string sql = "SELECT id, run_id, ts, level, source, type, message FROM events WHERE run_id = ? ORDER BY id ASC;";
    sqlite3_stmt* stmt = nullptr;
    nlohmann::json timeline = nlohmann::json::array();
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, runId);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            timeline.push_back({
                {"id", id},
                {"run_id", sqlite3_column_int(stmt, 1)},
                {"ts", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))},
                {"level", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))},
                {"source", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))},
                {"type", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))},
                {"message", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6))},
                {"in_chain", inChainEventIds.find(id) != inChainEventIds.end()}
            });
        }
        sqlite3_finalize(stmt);
    }

    return timeline;
}

} // namespace tracex
