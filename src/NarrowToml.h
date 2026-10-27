#pragma once

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Lexical subset shared by two independent schemas; values retain their TOML spelling.
namespace narrowToml {
struct Line { int number; std::string table, key, value; bool array = false; };
inline std::string trim(const std::string &s) {
    const auto first = s.find_first_not_of(" \t\r");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}
inline std::vector<Line> lines(const std::string &text) {
    std::vector<Line> result;
    std::istringstream input(text);
    std::string line;
    int number = 0;
    while (std::getline(input, line)) {
        ++number;
        bool quoted = false, escaped = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (c == '#' && !quoted) { line.resize(i); break; }
            if (c == '"' && !escaped) quoted = !quoted;
            if (quoted && c == '\\' && !escaped) escaped = true;
            else escaped = false;
        }
        line = trim(line);
        if (line.empty()) continue;
        const auto fail = [&]() { throw std::runtime_error("line " + std::to_string(number) + ": expected a supported table or key = scalar"); };
        if (line.front() == '[') {
            const bool array = line.size() > 1 && line[1] == '[';
            const std::size_t edge = array ? 2 : 1;
            if (line.size() <= 2 * edge || line.substr(line.size() - edge) != (array ? "]]" : "]")) fail();
            const auto table = trim(line.substr(edge, line.size() - 2 * edge));
            if (!std::regex_match(table, std::regex("[A-Za-z][A-Za-z0-9]*(\\.[A-Za-z][A-Za-z0-9]*)?"))) fail();
            result.push_back({number, table, "", "", array});
        } else {
            const auto equal = line.find('=');
            if (equal == std::string::npos) fail();
            const auto key = trim(line.substr(0, equal));
            const auto value = trim(line.substr(equal + 1));
            if (!std::regex_match(key, std::regex("[A-Za-z][A-Za-z0-9]*")) || value.empty()) fail();
            result.push_back({number, "", key, value, false});
        }
    }
    return result;
}
inline void write(const std::string &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) throw std::runtime_error("cannot write " + path);
}
}
