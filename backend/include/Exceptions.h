#pragma once

#include <exception>
#include <string>
#include <sstream>

namespace tracex {

class TraceXException : public std::exception {
protected:
    std::string message_;
    int statusCode_;
public:
    explicit TraceXException(std::string message, int statusCode = 500)
        : message_(std::move(message)), statusCode_(statusCode) {}

    const char* what() const noexcept override {
        return message_.c_str();
    }

    int statusCode() const noexcept {
        return statusCode_;
    }
};

class FileNotFoundException : public TraceXException {
public:
    explicit FileNotFoundException(const std::string& path)
        : TraceXException("File not found: " + path, 404) {}
};

class InvalidLogFormatException : public TraceXException {
private:
    size_t lineNumber_;
    std::string rawLine_;
public:
    InvalidLogFormatException(size_t lineNum, const std::string& line, const std::string& reason)
        : TraceXException(buildMessage(lineNum, line, reason), 400),
          lineNumber_(lineNum), rawLine_(line) {}

    size_t lineNumber() const noexcept { return lineNumber_; }
    const std::string& rawLine() const noexcept { return rawLine_; }

private:
    static std::string buildMessage(size_t lineNum, const std::string& line, const std::string& reason) {
        std::ostringstream oss;
        oss << "Invalid log format at line " << lineNum << ": " << reason;
        if (!line.empty()) {
            oss << " [Content: " << (line.length() > 60 ? line.substr(0, 57) + "..." : line) << "]";
        }
        return oss.str();
    }
};

class DatabaseException : public TraceXException {
public:
    explicit DatabaseException(const std::string& details)
        : TraceXException("Database error: " + details, 500) {}
};

class InvalidRequestException : public TraceXException {
public:
    explicit InvalidRequestException(const std::string& reason)
        : TraceXException("Invalid request: " + reason, 400) {}
};

} // namespace tracex
