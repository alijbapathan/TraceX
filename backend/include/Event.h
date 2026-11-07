#pragma once

#include <string>
#include <ctime>
#include <memory>
#include <iomanip>
#include <sstream>
#include "third_party/json.hpp"

#ifdef ERROR
#undef ERROR
#endif

namespace tracex {

enum class Level {
    INFO,
    WARN,
    ERROR,
    FATAL
};

enum class EventType {
    NORMAL,
    DB_FAILURE,
    API_FAILURE,
    NETWORK_FAILURE,
    MEMORY_PRESSURE,
    AUTH_FAILURE,
    OTHER
};

class Event {
public:
    int id;
    int runId;
    std::time_t timestamp;
    Level level;
    std::string source;
    std::string message;
    EventType type;

    Event()
        : id(0), runId(0), timestamp(0), level(Level::INFO), source(""), message(""), type(EventType::NORMAL) {}

    Event(int id, int runId, std::time_t ts, Level lvl, std::string src, std::string msg, EventType evType = EventType::NORMAL)
        : id(id), runId(runId), timestamp(ts), level(lvl), source(std::move(src)), message(std::move(msg)), type(evType) {}

    // Copy constructor for deep copying during What-if analysis
    Event(const Event& other) = default;
    Event& operator=(const Event& other) = default;

    std::unique_ptr<Event> clone() const {
        return std::make_unique<Event>(*this);
    }

    bool isError() const {
        return level == Level::ERROR || level == Level::FATAL;
    }

    static std::string levelToString(Level lvl) {
        switch (lvl) {
            case Level::INFO: return "INFO";
            case Level::WARN: return "WARN";
            case Level::ERROR: return "ERROR";
            case Level::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    static Level stringToLevel(const std::string& str) {
        if (str == "INFO") return Level::INFO;
        if (str == "WARN") return Level::WARN;
        if (str == "ERROR") return Level::ERROR;
        if (str == "FATAL") return Level::FATAL;
        return Level::INFO;
    }

    static std::string typeToString(EventType t) {
        switch (t) {
            case EventType::NORMAL: return "NORMAL";
            case EventType::DB_FAILURE: return "DB_FAILURE";
            case EventType::API_FAILURE: return "API_FAILURE";
            case EventType::NETWORK_FAILURE: return "NETWORK_FAILURE";
            case EventType::MEMORY_PRESSURE: return "MEMORY_PRESSURE";
            case EventType::AUTH_FAILURE: return "AUTH_FAILURE";
            case EventType::OTHER: return "OTHER";
            default: return "UNKNOWN";
        }
    }

    static EventType stringToType(const std::string& str) {
        if (str == "DB_FAILURE") return EventType::DB_FAILURE;
        if (str == "API_FAILURE") return EventType::API_FAILURE;
        if (str == "NETWORK_FAILURE") return EventType::NETWORK_FAILURE;
        if (str == "MEMORY_PRESSURE") return EventType::MEMORY_PRESSURE;
        if (str == "AUTH_FAILURE") return EventType::AUTH_FAILURE;
        if (str == "OTHER") return EventType::OTHER;
        return EventType::NORMAL;
    }

    static std::string formatTimestamp(std::time_t ts) {
        std::tm tm_buf{};
#if defined(_WIN32) || defined(_WIN64)
        gmtime_s(&tm_buf, &ts);
#else
        gmtime_r(&ts, &tm_buf);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    static std::time_t parseTimestamp(const std::string& str) {
        std::tm tm_buf{};
        std::istringstream iss(str);
        iss >> std::get_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        if (iss.fail()) {
            return 0;
        }
#if defined(_WIN32) || defined(_WIN64)
        return _mkgmtime(&tm_buf);
#else
        return timegm(&tm_buf);
#endif
    }

    std::string timestampString() const {
        return formatTimestamp(timestamp);
    }

    nlohmann::json toJson() const {
        return {
            {"id", id},
            {"run_id", runId},
            {"ts", timestampString()},
            {"timestamp", timestamp},
            {"level", levelToString(level)},
            {"source", source},
            {"type", typeToString(type)},
            {"message", message}
        };
    }
};

} // namespace tracex
