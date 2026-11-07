#include "LogParser.h"
#include "Utils.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace tracex {

std::vector<std::unique_ptr<Event>> LogParser::process(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw FileNotFoundException(filePath);
    }
    return parse(file);
}

std::vector<std::unique_ptr<Event>> LogParser::process(std::istream& is) {
    return parse(is);
}

std::vector<std::unique_ptr<Event>> LogParser::process(const std::vector<std::string>& lines) {
    std::string joined;
    for (const auto& line : lines) {
        joined += line;
        joined += "\n";
    }
    std::istringstream iss(joined);
    return parse(iss);
}

std::unique_ptr<LogParser> LogParser::createParser(const std::string& pathOrFormat, int runId) {
    std::string lower = toLower(pathOrFormat);
    if (lower == "csv" || (lower.length() >= 4 && lower.substr(lower.length() - 4) == ".csv")) {
        return std::make_unique<CsvLogParser>(runId);
    }
    if (lower == "json" || lower == "jsonl" ||
        (lower.length() >= 5 && lower.substr(lower.length() - 5) == ".json") ||
        (lower.length() >= 6 && lower.substr(lower.length() - 6) == ".jsonl")) {
        return std::make_unique<JsonLogParser>(runId);
    }
    return std::make_unique<TextLogParser>(runId);
}

// ---------------- TextLogParser ----------------

std::vector<std::unique_ptr<Event>> TextLogParser::parse(std::istream& is) {
    std::vector<std::unique_ptr<Event>> events;
    std::string line;
    size_t lineNum = 0;
    int eventId = 1;

    while (std::getline(is, line)) {
        ++lineNum;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue; // Skip comments and empty lines
        }

        try {
            auto ev = parseLine(trimmed, lineNum, eventId);
            if (ev) {
                classifier_.classifyEvent(*ev);
                events.push_back(std::move(ev));
                ++parsedCount_;
                ++eventId;
            }
        } catch (const InvalidLogFormatException& ex) {
            ++skippedCount_;
            errors_.push_back(ex.what());
        } catch (const std::exception& ex) {
            ++skippedCount_;
            errors_.push_back("Error parsing line " + std::to_string(lineNum) + ": " + ex.what());
        }
    }

    return events;
}

std::unique_ptr<Event> TextLogParser::parseLine(const std::string& line, size_t lineNum, int nextId) {
    // Format: 2026-10-21 10:00:05 LEVEL SOURCE Message...
    // Date: chars 0..9, Time: chars 11..18 -> length 19
    if (line.length() < 20) {
        throw InvalidLogFormatException(lineNum, line, "Line too short to contain timestamp");
    }

    // Basic date check: YYYY-MM-DD HH:MM:SS
    if (line[4] != '-' || line[7] != '-' || line[10] != ' ' || line[13] != ':' || line[16] != ':') {
        throw InvalidLogFormatException(lineNum, line, "Timestamp not in format YYYY-MM-DD HH:MM:SS");
    }

    std::string tsStr = line.substr(0, 19);
    time_t ts = Event::parseTimestamp(tsStr);
    if (ts == 0) {
        throw InvalidLogFormatException(lineNum, line, "Failed to parse timestamp: " + tsStr);
    }

    size_t pos = 20;
    while (pos < line.length() && line[pos] == ' ') ++pos;
    if (pos >= line.length()) {
        throw InvalidLogFormatException(lineNum, line, "Missing level and source");
    }

    // Extract Level
    size_t nextSpace = line.find(' ', pos);
    if (nextSpace == std::string::npos) {
        throw InvalidLogFormatException(lineNum, line, "Missing source and message");
    }
    std::string levelStr = line.substr(pos, nextSpace - pos);
    pos = nextSpace + 1;
    while (pos < line.length() && line[pos] == ' ') ++pos;

    // Extract Source
    nextSpace = line.find(' ', pos);
    std::string sourceStr;
    std::string messageStr;
    if (nextSpace == std::string::npos) {
        sourceStr = line.substr(pos);
        messageStr = "";
    } else {
        sourceStr = line.substr(pos, nextSpace - pos);
        pos = nextSpace + 1;
        while (pos < line.length() && line[pos] == ' ') ++pos;
        if (pos < line.length()) {
            messageStr = line.substr(pos);
        }
    }

    Level lvl = Event::stringToLevel(levelStr);
    return std::make_unique<Event>(nextId, defaultRunId_, ts, lvl, sourceStr, messageStr);
}

// ---------------- CsvLogParser ----------------

std::vector<std::unique_ptr<Event>> CsvLogParser::parse(std::istream& is) {
    std::vector<std::unique_ptr<Event>> events;
    std::string line;
    size_t lineNum = 0;
    int eventId = 1;

    while (std::getline(is, line)) {
        ++lineNum;
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        // Skip header if line 1 has "timestamp"
        if (lineNum == 1 && toLower(trimmed).find("timestamp") != std::string::npos) {
            continue;
        }

        try {
            auto ev = parseLine(trimmed, lineNum, eventId);
            if (ev) {
                classifier_.classifyEvent(*ev);
                events.push_back(std::move(ev));
                ++parsedCount_;
                ++eventId;
            }
        } catch (const InvalidLogFormatException& ex) {
            ++skippedCount_;
            errors_.push_back(ex.what());
        } catch (const std::exception& ex) {
            ++skippedCount_;
            errors_.push_back("Error parsing line " + std::to_string(lineNum) + ": " + ex.what());
        }
    }

    return events;
}

std::unique_ptr<Event> CsvLogParser::parseLine(const std::string& line, size_t lineNum, int nextId) {
    // Format: timestamp,level,source,message
    std::vector<std::string> fields;
    std::string current;
    bool inQuotes = false;

    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.length() && line[i + 1] == '"') {
                current += '"';
                ++i; // skip escaped quote
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            fields.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    fields.push_back(trim(current));

    if (fields.size() < 4) {
        throw InvalidLogFormatException(lineNum, line, "CSV line does not have 4 fields (timestamp, level, source, message)");
    }

    time_t ts = Event::parseTimestamp(fields[0]);
    if (ts == 0) {
        throw InvalidLogFormatException(lineNum, line, "Invalid timestamp: " + fields[0]);
    }

    Level lvl = Event::stringToLevel(fields[1]);
    return std::make_unique<Event>(nextId, defaultRunId_, ts, lvl, fields[2], fields[3]);
}

// ---------------- JsonLogParser ----------------

std::vector<std::unique_ptr<Event>> JsonLogParser::parse(std::istream& is) {
    std::vector<std::unique_ptr<Event>> events;
    std::string line;
    size_t lineNum = 0;
    int eventId = 1;

    while (std::getline(is, line)) {
        ++lineNum;
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        try {
            auto ev = parseLine(trimmed, lineNum, eventId);
            if (ev) {
                classifier_.classifyEvent(*ev);
                events.push_back(std::move(ev));
                ++parsedCount_;
                ++eventId;
            }
        } catch (const InvalidLogFormatException& ex) {
            ++skippedCount_;
            errors_.push_back(ex.what());
        } catch (const std::exception& ex) {
            ++skippedCount_;
            errors_.push_back("Error parsing line " + std::to_string(lineNum) + ": " + ex.what());
        }
    }

    return events;
}

std::unique_ptr<Event> JsonLogParser::parseLine(const std::string& line, size_t lineNum, int nextId) {
    try {
        auto j = nlohmann::json::parse(line);
        std::string tsStr = j.value("ts", j.value("timestamp", ""));
        std::string levelStr = j.value("level", "INFO");
        std::string sourceStr = j.value("source", "SYSTEM");
        std::string msgStr = j.value("msg", j.value("message", ""));

        time_t ts = Event::parseTimestamp(tsStr);
        if (ts == 0) {
            // Try unix epoch if numerical
            if (j.contains("timestamp") && j["timestamp"].is_number()) {
                ts = static_cast<time_t>(j["timestamp"].get<long long>());
            } else {
                throw InvalidLogFormatException(lineNum, line, "Invalid timestamp in JSON: " + tsStr);
            }
        }

        Level lvl = Event::stringToLevel(levelStr);
        return std::make_unique<Event>(nextId, defaultRunId_, ts, lvl, sourceStr, msgStr);
    } catch (const nlohmann::json::exception& ex) {
        throw InvalidLogFormatException(lineNum, line, std::string("Malformed JSON: ") + ex.what());
    }
}

} // namespace tracex
