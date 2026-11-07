#pragma once

#include <string>
#include <vector>
#include <memory>
#include "Event.h"
#include "AnalysisStrategy.h"
#include "Database.h"
#include "third_party/json.hpp"

namespace tracex {

class Report {
private:
    RunRecord runInfo_;
    AnalysisResult analysisResult_;
    std::vector<std::unique_ptr<Event>> events_;

public:
    Report(RunRecord run, AnalysisResult result, std::vector<std::unique_ptr<Event>> events);

    std::string toText() const;
    nlohmann::json toJson() const;

    std::string saveToFile(const std::string& directory = "data", const std::string& format = "txt") const;

    const AnalysisResult& getAnalysisResult() const noexcept { return analysisResult_; }
    const RunRecord& getRunRecord() const noexcept { return runInfo_; }
};

} // namespace tracex
