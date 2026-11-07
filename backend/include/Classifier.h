#pragma once

#include "Event.h"
#include <string>
#include <vector>

namespace tracex {

class Classifier {
public:
    Classifier() = default;

    // Classifies an event and assigns its EventType based on source and keyword heuristics
    EventType classify(const std::string& source, const std::string& message, Level level) const;
    void classifyEvent(Event& event) const;

    // Helper to get descriptive human explanation for an event type
    static std::string getTypeDescription(EventType type);

private:
    static bool containsAny(const std::string& text, const std::vector<std::string>& keywords);
};

} // namespace tracex
