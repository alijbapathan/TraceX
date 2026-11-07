#pragma once

#include <vector>
#include <map>
#include <algorithm>
#include <string>
#include <sstream>
#include <type_traits>
#include <cctype>

namespace tracex {

/**
 * Generic Template Utilities for filtering, grouping, sorting, and counting.
 * Demonstrates advanced C++ template programming and generic algorithms.
 */

template<typename T, typename Pred>
std::vector<T> filterBy(const std::vector<T>& items, Pred predicate) {
    std::vector<T> result;
    for (const auto& item : items) {
        if (predicate(item)) {
            result.push_back(item);
        }
    }
    return result;
}

template<typename T, typename KeyFunc>
auto groupBy(const std::vector<T>& items, KeyFunc keyFunc) 
    -> std::map<decltype(keyFunc(items.empty() ? std::declval<T>() : items.front())), std::vector<T>> {
    using KeyType = decltype(keyFunc(items.empty() ? std::declval<T>() : items.front()));
    std::map<KeyType, std::vector<T>> grouped;
    for (const auto& item : items) {
        grouped[keyFunc(item)].push_back(item);
    }
    return grouped;
}

template<typename T, typename Compare>
void sortBy(std::vector<T>& items, Compare comp) {
    std::sort(items.begin(), items.end(), comp);
}

template<typename T, typename Pred>
size_t countIf(const std::vector<T>& items, Pred predicate) {
    size_t count = 0;
    for (const auto& item : items) {
        if (predicate(item)) {
            ++count;
        }
    }
    return count;
}

// String utility functions
inline std::string toLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return str;
}

inline std::string trim(const std::string& str) {
    auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

inline std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

} // namespace tracex
