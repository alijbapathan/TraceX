#include <iostream>
#include <cassert>
#include <sstream>
#include <memory>
#include "Event.h"
#include "Exceptions.h"
#include "LogParser.h"
#include "Classifier.h"
#include "AnalysisStrategy.h"
#include "Analyzer.h"
#include "Utils.h"

using namespace tracex;

void testTemplatesAndUtils() {
    std::cout << "[Test] Running Generic Templates and Utilities tests... ";

    std::vector<int> numbers = {5, 2, 8, 1, 9, 4};
    // filterBy
    auto evens = filterBy(numbers, [](int n) { return n % 2 == 0; });
    assert(evens.size() == 3);
    assert(evens[0] == 2 && evens[1] == 8 && evens[2] == 4);

    // countIf
    size_t greaterThanFour = countIf(numbers, [](int n) { return n > 4; });
    assert(greaterThanFour == 3);

    // sortBy
    sortBy(numbers, [](int a, int b) { return a < b; });
    assert(numbers.front() == 1);
    assert(numbers.back() == 9);

    // groupBy
    auto grouped = groupBy(numbers, [](int n) { return (n % 2 == 0) ? "even" : "odd"; });
    assert(grouped["even"].size() == 3);
    assert(grouped["odd"].size() == 3);

    std::cout << "PASSED\n";
}

void testEventAndClassifier() {
    std::cout << "[Test] Running Event and Classifier tests... ";

    Classifier classifier;

    // Test DB failure classification
    EventType t1 = classifier.classify("DATABASE", "Connection timeout: pool exhausted", Level::ERROR);
    assert(t1 == EventType::DB_FAILURE);

    // Test Memory pressure classification
    EventType t2 = classifier.classify("MEMORY", "OutOfMemory error: heap exhausted", Level::FATAL);
    assert(t2 == EventType::MEMORY_PRESSURE);

    // Test Network failure classification
    EventType t3 = classifier.classify("NETWORK", "Connection refused: gateway unreachable", Level::ERROR);
    assert(t3 == EventType::NETWORK_FAILURE);

    // Test Auth failure classification
    EventType t4 = classifier.classify("AUTH", "Token verification failed: 401 Unauthorized", Level::ERROR);
    assert(t4 == EventType::AUTH_FAILURE);

    // Test Normal classification
    EventType t5 = classifier.classify("API", "GET /api/v1/health status=200", Level::INFO);
    assert(t5 == EventType::NORMAL);

    // Test Event clone (deep copy)
    Event e1(42, 1, 1700000000, Level::ERROR, "DATABASE", "DB timeout", EventType::DB_FAILURE);
    auto cloned = e1.clone();
    assert(cloned->id == 42);
    assert(cloned->source == "DATABASE");
    assert(cloned->type == EventType::DB_FAILURE);

    std::cout << "PASSED\n";
}

void testParsers() {
    std::cout << "[Test] Running Text, CSV, and JSON LogParser tests... ";

    // 1. Text Log Parser
    {
        std::string textLogs = 
            "2026-10-21 10:00:00 INFO API System healthy\n"
            "2026-10-21 10:00:05 ERROR DATABASE Connection timeout\n"
            "invalid line with no timestamp\n"
            "2026-10-21 10:00:07 ERROR API 500 Internal Server Error\n";

        TextLogParser parser;
        std::istringstream iss(textLogs);
        auto events = parser.parse(iss);

        assert(events.size() == 3);
        assert(parser.getSkippedCount() == 1);
        assert(events[0]->level == Level::INFO);
        assert(events[1]->level == Level::ERROR);
        assert(events[1]->source == "DATABASE");
        assert(events[1]->type == EventType::DB_FAILURE);
    }

    // 2. CSV Log Parser
    {
        std::string csvLogs = 
            "timestamp,level,source,message\n"
            "2026-10-21 10:00:00,INFO,API,System healthy\n"
            "2026-10-21 10:00:05,ERROR,DATABASE,\"Connection timeout, pool exhausted\"\n";

        CsvLogParser parser;
        std::istringstream iss(csvLogs);
        auto events = parser.parse(iss);

        assert(events.size() == 2);
        assert(events[1]->source == "DATABASE");
        assert(events[1]->message == "Connection timeout, pool exhausted");
    }

    // 3. JSON Log Parser
    {
        std::string jsonLogs = 
            "{\"ts\":\"2026-10-21 10:00:00\",\"level\":\"INFO\",\"source\":\"API\",\"msg\":\"System healthy\"}\n"
            "{\"ts\":\"2026-10-21 10:00:05\",\"level\":\"FATAL\",\"source\":\"MEMORY\",\"msg\":\"OutOfMemory heap exhausted\"}\n"
            "{invalid json line}\n";

        JsonLogParser parser;
        std::istringstream iss(jsonLogs);
        auto events = parser.parse(iss);

        assert(events.size() == 2);
        assert(parser.getSkippedCount() == 1);
        assert(events[1]->level == Level::FATAL);
        assert(events[1]->source == "MEMORY");
        assert(events[1]->type == EventType::MEMORY_PRESSURE);
    }

    std::cout << "PASSED\n";
}

void testStrategiesAndAnalyzer() {
    std::cout << "[Test] Running AnalysisStrategy and Analyzer tests... ";

    std::vector<std::unique_ptr<Event>> events;
    time_t baseTime = 1729500000;

    // Normal noise
    events.push_back(std::make_unique<Event>(1, 1, baseTime, Level::INFO, "API", "GET /healthy", EventType::NORMAL));
    // Injected DB cascade
    events.push_back(std::make_unique<Event>(2, 1, baseTime + 5, Level::ERROR, "DATABASE", "Connection timeout: pool exhausted", EventType::DB_FAILURE));
    events.push_back(std::make_unique<Event>(3, 1, baseTime + 7, Level::ERROR, "API", "GET /orders 500 Internal Server Error", EventType::API_FAILURE));
    events.push_back(std::make_unique<Event>(4, 1, baseTime + 9, Level::ERROR, "API", "POST /checkout 503 Service Unavailable", EventType::API_FAILURE));

    // Test Rule-Based Strategy
    Analyzer analyzer(std::make_unique<RuleBasedStrategy>());
    AnalysisResult ruleResult = analyzer.analyze(events);

    assert(ruleResult.rootCauseType == "DB_FAILURE");
    assert(ruleResult.rootEventId == 2);
    assert(ruleResult.supportingEventIds.size() == 2);
    assert(ruleResult.confidence > 70.0);
    assert(ruleResult.failureChains.size() >= 1);

    // Test Time-Window Strategy
    analyzer.setStrategy(std::make_unique<TimeWindowStrategy>(15));
    AnalysisResult windowResult = analyzer.analyze(events);
    assert(windowResult.rootEventId == 2);
    assert(windowResult.confidence > 40.0);

    // Test What-If Analysis
    // Remove the DB timeout event (#2) and see what happens
    WhatIfResult whatif = analyzer.runWhatIf(events, {2});
    assert(whatif.removedEventIds.size() == 1);
    assert(whatif.rootCauseChanged); // Root cause should have shifted away from event #2
    assert(whatif.modifiedResult.rootEventId != 2);

    // Test Algorithm Comparison
    auto comp = analyzer.compareAlgorithms(events);
    assert(comp.comparisons.size() == 2);

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "================================================================================\n";
    std::cout << "                  TRACEX UNIT TESTS SUITE                                       \n";
    std::cout << "================================================================================\n";

    try {
        testTemplatesAndUtils();
        testEventAndClassifier();
        testParsers();
        testStrategiesAndAnalyzer();

        std::cout << "================================================================================\n";
        std::cout << "                  ALL TESTS PASSED SUCCESSFULLY!                                \n";
        std::cout << "================================================================================\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "\n[TEST FAILED] Exception: " << ex.what() << "\n";
        return 1;
    }
}
