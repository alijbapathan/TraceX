#include "Classifier.h"
#include "Utils.h"

namespace tracex {

bool Classifier::containsAny(const std::string& text, const std::vector<std::string>& keywords) {
    std::string lowerText = toLower(text);
    for (const auto& kw : keywords) {
        if (lowerText.find(toLower(kw)) != std::string::npos) {
            return true;
        }
    }
    return false;
}

EventType Classifier::classify(const std::string& source, const std::string& message, Level level) const {
    if (level == Level::INFO && !containsAny(message, {"timeout", "fatal", "fail", "refused", "crash", "outofmemory"})) {
        return EventType::NORMAL;
    }

    std::string src = toLower(source);
    std::string msg = toLower(message);

    // Database failure: source is database with error/failure keywords, or message has DB keywords
    if ((src == "database" && (level == Level::ERROR || level == Level::FATAL || containsAny(msg, {"timeout", "deadlock", "pool", "refused", "fail", "error", "closed"}))) ||
        containsAny(msg, {"connection timeout", "pool exhausted", "deadlock", "sql error", "replica down", "query cancelled", "database query execution failed", "database host"})) {
        return EventType::DB_FAILURE;
    }

    // Memory pressure: keywords related to memory exhaustion
    if (containsAny(msg, {"outofmemory", "oom", "heap exhausted", "high usage", "memory consumption exceeded", "allocation failed", "sigabrt received from native memory", "heap", "leak"})) {
        return EventType::MEMORY_PRESSURE;
    }

    // Network failure: keywords related to network disconnects/gateways
    if ((src == "network" && (level == Level::ERROR || level == Level::FATAL || containsAny(msg, {"unreachable", "refused", "loss", "reset", "failed", "error"}))) ||
        containsAny(msg, {"connection refused", "gateway unreachable", "dns failure", "packet loss", "socket reset", "socket error", "502 bad gateway", "504 gateway timeout"})) {
        return EventType::NETWORK_FAILURE;
    }

    // Auth failure: keywords related to authentication/authorization
    if ((src == "auth" && (level == Level::ERROR || level == Level::FATAL || containsAny(msg, {"unauthorized", "forbidden", "token", "credential", "failed"}))) ||
        containsAny(msg, {"token verification failed", "unauthorized", "invalid credentials", "401", "403 forbidden", "auth service", "token expired"})) {
        return EventType::AUTH_FAILURE;
    }

    // API failure: 5xx errors or service crashes
    if ((src == "api" && (level == Level::ERROR || level == Level::FATAL)) ||
        containsAny(msg, {"500 internal server error", "503 service unavailable", "endpoint failed", "process crash", "500", "502", "503", "504"})) {
        return EventType::API_FAILURE;
    }

    if (level == Level::ERROR || level == Level::FATAL) {
        return EventType::OTHER;
    }

    return EventType::NORMAL;
}

void Classifier::classifyEvent(Event& event) const {
    event.type = classify(event.source, event.message, event.level);
}

std::string Classifier::getTypeDescription(EventType type) {
    switch (type) {
        case EventType::DB_FAILURE: return "Database Failure (timeouts, connection pool exhaustion, deadlock)";
        case EventType::API_FAILURE: return "API Service Failure (5xx errors, endpoint crash)";
        case EventType::NETWORK_FAILURE: return "Network Outage (socket reset, gateway unreachable, DNS failure)";
        case EventType::MEMORY_PRESSURE: return "Memory Pressure (OutOfMemory, heap allocation failures)";
        case EventType::AUTH_FAILURE: return "Authentication/Authorization Failure (401/403 bursts, token rejection)";
        case EventType::OTHER: return "General/Unclassified Failure";
        case EventType::NORMAL: return "Normal Operational Activity";
        default: return "Unknown";
    }
}

} // namespace tracex
