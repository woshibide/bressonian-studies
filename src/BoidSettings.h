#pragma once

#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

// Shared interactive tuning and self-contained offline render snapshots.
struct BoidSettings {
    int boidCellSize = 16;
    float boidAlignment = 0.8f;
    float boidCohesion = 0.04f;
    float boidSeparation = 0.10f;
    float boidCrowdCount = 24.0f;
    float boidSwirl = 0.1f;
    float boidSwirlDirection = 1.0f;

    static bool isKey(const std::string &key) {
        return key == "boidCellSize" || key == "boidAlignment" || key == "boidCohesion" ||
            key == "boidSeparation" || key == "boidCrowdCount" || key == "boidSwirl" ||
            key == "boidSwirlDirection";
    }

    static BoidSettings parse(const std::map<std::string, std::string> &values) {
        for (const auto &value : values)
            if (!isKey(value.first)) throw std::runtime_error("unknown boids key '" + value.first + "'");
        const auto number = [&](const std::string &key, double fallback, double low, double high, bool integer = false) {
            const auto found = values.find(key);
            if (found == values.end()) return fallback;
            static const std::regex decimal(R"([+-]?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?)");
            static const std::regex whole(R"([+-]?(0|[1-9][0-9]*))");
            double result = 0;
            bool valid = std::regex_match(found->second, integer ? whole : decimal);
            if (valid) {
                try { result = std::stod(found->second); } catch (...) { valid = false; }
            }
            if (!valid || !std::isfinite(result) || result < low || result > high)
                throw std::runtime_error(key + ": expected " + (integer ? "integer" : "number") +
                    " in [" + std::to_string(low) + ", " + std::to_string(high) + "]");
            return result;
        };
        BoidSettings result;
        // A lower bound avoids explosive grid allocations at video resolutions.
        result.boidCellSize = static_cast<int>(number("boidCellSize", result.boidCellSize, 8, 512, true));
        result.boidAlignment = static_cast<float>(number("boidAlignment", result.boidAlignment, 0, 100));
        result.boidCohesion = static_cast<float>(number("boidCohesion", result.boidCohesion, 0, 100));
        result.boidSeparation = static_cast<float>(number("boidSeparation", result.boidSeparation, 0, 100));
        result.boidCrowdCount = static_cast<float>(number("boidCrowdCount", result.boidCrowdCount, 1, 1000000));
        result.boidSwirl = static_cast<float>(number("boidSwirl", result.boidSwirl, 0, 100));
        const double direction = number("boidSwirlDirection", result.boidSwirlDirection, -1, 1);
        if (direction != -1 && direction != 1)
            throw std::runtime_error("boidSwirlDirection: expected -1 or 1");
        result.boidSwirlDirection = static_cast<float>(direction);
        return result;
    }

    std::string serialize() const {
        std::ostringstream out;
        out << std::setprecision(std::numeric_limits<float>::max_digits10)
            << "boidCellSize = " << boidCellSize << '\n'
            << "boidAlignment = " << boidAlignment << '\n'
            << "boidCohesion = " << boidCohesion << '\n'
            << "boidSeparation = " << boidSeparation << '\n'
            << "boidCrowdCount = " << boidCrowdCount << '\n'
            << "boidSwirl = " << boidSwirl << '\n'
            << "boidSwirlDirection = " << boidSwirlDirection << '\n';
        return out.str();
    }
};
