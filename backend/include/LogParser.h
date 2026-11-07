#pragma once

#include <vector>
#include <memory>
#include <string>
#include <istream>
#include "Event.h"
#include "Exceptions.h"
#include "Classifier.h"

namespace tracex {

/**
 * Abstract Base Class for Log Parsers.
 * Demonstrates Object-Oriented polymorphism, template/strategy usage, and streaming file I/O.
 */
class LogParser {
protected:
    Classifier classifier_;
    size_t parsedCount_ = 0;
    size_t skippedCount_ = 0;
    std::vector<std::string> errors_;
    int defaultRunId_ = 1;

public:
    explicit LogParser(int runId = 1) : defaultRunId_(runId) {}
    virtual ~LogParser() = default;

    // Pure virtual method that concrete parsers must implement
    virtual std::vector<std::unique_ptr<Event>> parse(std::istream& is) = 0;

    // Overloaded entry points
    virtual std::vector<std::unique_ptr<Event>> process(const std::string& filePath);
    virtual std::vector<std::unique_ptr<Event>> process(std::istream& is);
    virtual std::vector<std::unique_ptr<Event>> process(const std::vector<std::string>& lines);

    size_t getParsedCount() const noexcept { return parsedCount_; }
    size_t getSkippedCount() const noexcept { return skippedCount_; }
    const std::vector<std::string>& getErrors() const noexcept { return errors_; }
    void setRunId(int id) noexcept { defaultRunId_ = id; }

    static std::unique_ptr<LogParser> createParser(const std::string& pathOrFormat, int runId = 1);

protected:
    virtual std::unique_ptr<Event> parseLine(const std::string& line, size_t lineNum, int nextId) = 0;
};

/**
 * TextLogParser:
 * Parses standard space-delimited text logs:
 * `2026-10-21 10:00:05 ERROR DATABASE Connection timeout`
 */
class TextLogParser : public LogParser {
public:
    explicit TextLogParser(int runId = 1) : LogParser(runId) {}
    std::vector<std::unique_ptr<Event>> parse(std::istream& is) override;

protected:
    std::unique_ptr<Event> parseLine(const std::string& line, size_t lineNum, int nextId) override;
};

/**
 * CsvLogParser:
 * Parses CSV logs:
 * `timestamp,level,source,message`
 */
class CsvLogParser : public LogParser {
public:
    explicit CsvLogParser(int runId = 1) : LogParser(runId) {}
    std::vector<std::unique_ptr<Event>> parse(std::istream& is) override;

protected:
    std::unique_ptr<Event> parseLine(const std::string& line, size_t lineNum, int nextId) override;
};

/**
 * JsonLogParser:
 * Parses JSON lines:
 * `{"ts": "2026-10-21 10:00:05", "level": "ERROR", "source": "DATABASE", "msg": "..."}`
 */
class JsonLogParser : public LogParser {
public:
    explicit JsonLogParser(int runId = 1) : LogParser(runId) {}
    std::vector<std::unique_ptr<Event>> parse(std::istream& is) override;

protected:
    std::unique_ptr<Event> parseLine(const std::string& line, size_t lineNum, int nextId) override;
};

} // namespace tracex
