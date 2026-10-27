#pragma once

#include "NarrowToml.h"
#include "BoidSettings.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

// Validated effective settings; table inheritance is handled by InteractiveConfig.
struct InteractiveSettings {
    int frameOffset = 3;
    float pixelFadePercent = 2.5f;
    float pixelFlightStrength = 1.0f;
    float trailSpeedFactor = 0.15f;
    std::string mode = "motionMask";
    std::string treatment = "smear";
    bool colouredParticles = false;
    bool particlesOnly = false;
    bool boids = false;
    bool useSixtyFps = false;
    bool hysteresisThresholding = false;
    bool polygonGate = false;
    float polygonDetail = 0.0f;
    bool objectCutout = false;
    float speedContrast = 0.0f;
        BoidSettings boidTuning;

    static std::string read(const std::string &path, std::size_t limit = 65536) {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("cannot open " + path + "; restore the file");
        std::string text;
        char buffer[4096];
        while (file.read(buffer, sizeof(buffer)) || file.gcount()) {
            text.append(buffer, static_cast<std::size_t>(file.gcount()));
            if (text.size() > limit) throw std::runtime_error("file exceeds " + std::to_string(limit) + " bytes");
        }
        if (!file.eof()) throw std::runtime_error("cannot read file");
        return text;
    }

    static InteractiveSettings parse(const std::string &text) {
        const auto trim = [](const std::string &s) {
            const auto first = s.find_first_not_of(" \t\r");
            return first == std::string::npos ? std::string() :
                s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
        };
        std::map<std::string, std::string> values;
        std::istringstream input(text);
        std::string line;
        int lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            bool quoted = false;
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '"') quoted = !quoted;
                if (line[i] == '#' && !quoted) { line.resize(i); break; }
            }
            line = trim(line);
            if (line.empty()) continue;
            const auto equal = line.find('=');
            const auto fail = [&](const std::string &reason) {
                throw std::runtime_error("line " + std::to_string(lineNumber) + ": " + reason);
            };
            if (equal == std::string::npos) fail("expected key = value");
            const std::string key = trim(line.substr(0, equal));
            const std::string value = trim(line.substr(equal + 1));
            if (key != "frameOffset" && key != "pixelFadePercent" &&
                key != "pixelFlightStrength" && key != "trailSpeedFactor" &&
                key != "mode" && key != "treatment" && key != "colouredParticles" &&
                key != "particlesOnly" && key != "boids" && key != "useSixtyFps" &&
                key != "hysteresisThresholding" && key != "polygonGate" &&
                key != "polygonDetail" && key != "objectCutout" &&
                key != "speedContrast") {
                fail("unknown key '" + key + "'");
            }
            if (!values.emplace(key, value).second) fail("duplicate key '" + key + "'");
        }
        const auto get = [&](const std::string &key) -> const std::string & {
            const auto found = values.find(key);
            if (found == values.end()) throw std::runtime_error("missing required key '" + key + "'");
            return found->second;
        };
        const auto number = [&](const std::string &key, double low, double high, bool integer) {
            static const std::regex decimal(R"([+-]?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?)");
            static const std::regex whole(R"([+-]?(0|[1-9][0-9]*))");
            const auto &value = get(key);
            double result = 0;
            bool valid = std::regex_match(value, integer ? whole : decimal);
            if (valid) {
                try { result = std::stod(value); } catch (...) { valid = false; }
            }
            if (!valid || !std::isfinite(result) || result < low || result > high) {
                throw std::runtime_error(key + ": expected " + (integer ? "integer" : "number") +
                    " in [" + std::to_string(low) + ", " + std::to_string(high) + "]");
            }
            return result;
        };
        const auto boolean = [&](const std::string &key) {
            const auto &value = get(key);
            if (value != "true" && value != "false")
                throw std::runtime_error(key + ": expected true or false");
            return value == "true";
        };
        const auto choice = [&](const std::string &key, const std::string &choices) {
            const auto &value = get(key);
            if (value.size() < 2 || value.front() != '"' || value.back() != '"')
                throw std::runtime_error(key + ": expected a double-quoted name: " + choices);
            const std::string name = value.substr(1, value.size() - 2);
            if (choices.find("|" + name + "|") == std::string::npos || name.empty() ||
                name.find('|') != std::string::npos)
                throw std::runtime_error(key + ": choose " + choices);
            return name;
        };
        InteractiveSettings result;
        result.frameOffset = static_cast<int>(number("frameOffset", 0, 120, true));
        result.pixelFadePercent = static_cast<float>(number("pixelFadePercent", 0, 100, false));
        result.pixelFlightStrength = static_cast<float>(number("pixelFlightStrength", -100, 100, false));
        result.trailSpeedFactor = static_cast<float>(number("trailSpeedFactor", 0, 100, false));
        result.mode = choice("mode", "|motionMask||motionPolygons||invertedGrayscale||displacementMap||displacedVideo||originalVideo|");
        result.treatment = choice("treatment", "|smear||streak||stretch|");
        result.colouredParticles = boolean("colouredParticles");
        result.particlesOnly = boolean("particlesOnly");
        result.boids = boolean("boids");
        result.useSixtyFps = boolean("useSixtyFps");
        if (values.count("hysteresisThresholding"))
            result.hysteresisThresholding = boolean("hysteresisThresholding");
        if (values.count("polygonGate"))
            result.polygonGate = boolean("polygonGate");
        if (values.count("polygonDetail"))
            result.polygonDetail = static_cast<float>(number("polygonDetail", 0, 100, false));
        if (values.count("objectCutout"))
            result.objectCutout = boolean("objectCutout");
        if (values.count("speedContrast"))
            result.speedContrast = static_cast<float>(number("speedContrast", 0, 100, false));
        return result;
    }

    std::string serialize() const {
        std::ostringstream out;
        out << std::setprecision(std::numeric_limits<float>::max_digits10) << std::boolalpha
            << "# Interactive settings; decimal numbers have no C++ f suffix.\n"
            << "frameOffset = " << frameOffset << '\n'
            << "pixelFadePercent = " << pixelFadePercent << '\n'
            << "pixelFlightStrength = " << pixelFlightStrength << '\n'
            << "trailSpeedFactor = " << trailSpeedFactor << '\n'
            << "mode = \"" << mode << "\"\n"
            << "treatment = \"" << treatment << "\"\n"
            << "colouredParticles = " << colouredParticles << '\n'
            << "particlesOnly = " << particlesOnly << '\n'
            << "boids = " << boids << '\n'
            << "useSixtyFps = " << useSixtyFps << '\n'
            << "hysteresisThresholding = " << hysteresisThresholding << '\n'
            << "polygonGate = " << polygonGate << '\n'
            << "polygonDetail = " << polygonDetail << '\n'
            << "objectCutout = " << objectCutout << '\n'
            << "speedContrast = " << speedContrast << '\n';
        return out.str();
    }
};

struct InteractiveConfig {
    using Values = std::map<std::string, std::string>;
    Values global;
    Values boidValues;
    std::map<std::string, Values> overrides;

    static bool validMode(const std::string &mode) {
        return mode == "motionMask" || mode == "motionPolygons" || mode == "invertedGrayscale" ||
            mode == "displacementMap" || mode == "displacedVideo" || mode == "originalVideo";
    }
    static std::string flat(const Values &values) {
        std::string text;
        for (const auto &value : values) text += value.first + " = " + value.second + "\n";
        return text;
    }
    static Values scalars(const InteractiveSettings &settings) {
        Values result;
        for (const auto &line : narrowToml::lines(settings.serialize())) result[line.key] = line.value;
        return result;
    }
    InteractiveSettings resolve(const std::string &mode) const {
        Values values = global;
        const auto found = overrides.find(mode);
        if (found != overrides.end()) for (const auto &value : found->second) values[value.first] = value.second;
        values["mode"] = "\"" + mode + "\"";
        auto settings = InteractiveSettings::parse(flat(values));
                settings.boidTuning = BoidSettings::parse(boidValues);
                return settings;
    }
    std::string selectedMode() const { return InteractiveSettings::parse(flat(global)).mode; }
    static InteractiveConfig parse(const std::string &text) {
        InteractiveConfig result;
        Values *values = &result.global;
        std::map<std::string, bool> tables;
        bool flatKeys = false;
        for (const auto &line : narrowToml::lines(text)) {
            try {
                if (!line.table.empty()) {
                    if (line.array || (line.table != "global" && line.table != "boids" && !validMode(line.table)))
                        throw std::runtime_error("unknown settings table '" + line.table + "'");
                    if (!tables.emplace(line.table, true).second || (line.table == "global" && flatKeys))
                        throw std::runtime_error("duplicate settings table");
                    values = line.table == "global" ? &result.global :
                                            line.table == "boids" ? &result.boidValues : &result.overrides[line.table];
                } else {
                    if (tables.empty()) flatKeys = true;
                    if (values != &result.global && line.key == "mode")
                        throw std::runtime_error("mode belongs only in [global]");
                    if (!values->emplace(line.key, line.value).second)
                        throw std::runtime_error("duplicate key '" + line.key + "'");
                }
            } catch (const std::exception &error) {
                throw std::runtime_error("line " + std::to_string(line.number) + ": " + error.what());
            }
        }
        result.selectedMode();
        BoidSettings::parse(result.boidValues);
        result.global.emplace("hysteresisThresholding", "false");
        result.global.emplace("polygonGate", "false");
        result.global.emplace("polygonDetail", "0");
        result.global.emplace("objectCutout", "false");
        result.global.emplace("speedContrast", "0");
        for (const auto &entry : result.overrides) {
            try { result.resolve(entry.first); }
            catch (const std::exception &error) { throw std::runtime_error("[" + entry.first + "]: " + error.what()); }
        }
        return result;
    }
    void capture(const InteractiveSettings &settings) {
        global["mode"] = "\"" + settings.mode + "\"";
        boidValues.clear();
        for (const auto &line : narrowToml::lines(settings.boidTuning.serialize()))
            boidValues[line.key] = line.value;
        auto values = scalars(settings);
        values.erase("mode");
        // Only the selected mode changes; global defaults and other overrides stay intact.
        auto &target = overrides[settings.mode];
        for (const auto &value : values) {
            if (global.at(value.first) != value.second) target[value.first] = value.second;
            else target.erase(value.first);
        }
    }
    std::string serialize() const {
        std::string text = "# Interactive settings; numbers use TOML syntax (2.5, not 2.5f).\n[global]\n" + flat(global);
        for (const auto &entry : overrides) text += "\n[" + entry.first + "]\n" + flat(entry.second);
        text += "\n[boids]\n" + flat(boidValues);
        return text;
    }
};
