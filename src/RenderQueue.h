#pragma once

#include "ofMain.h"
#include "InteractiveSettings.h"
#include "BoidSettings.h"
#include <filesystem>
#include <map>
#include <set>
#include <unistd.h>

// JSON is an in-memory tree only; disk queues use the independent render TOML schema.
namespace renderQueue {
inline ofJson scalar(const std::string &value) {
    if (value == "true") return true;
    if (value == "false") return false;
    if (value.front() == '"') {
        auto result = ofJson::parse(value);
        if (!result.is_string()) throw std::runtime_error("expected a quoted string");
        return result;
    }
    static const std::regex number(R"([+-]?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?)");
    if (!std::regex_match(value, number)) throw std::runtime_error("expected a decimal number, boolean or double-quoted string (no f suffix)");
    if (value.find_first_of(".eE") == std::string::npos) return std::stoll(value);
    const double result = std::stod(value);
    if (!std::isfinite(result)) throw std::runtime_error("number must be finite");
    return result;
}
inline void validate(const ofJson &document) {
    if (!document.is_object() || !document.contains("version") ||
            !document.at("version").is_number_integer() || document.at("version") != 1 ||
        !document.contains("videos") || !document.at("videos").is_array())
        throw std::runtime_error("expected version = 1 and [[videos]] entries");
    for (auto it = document.begin(); it != document.end(); ++it)
        if (it.key() != "version" && it.key() != "videos") throw std::runtime_error("unknown queue key: " + it.key());
    const std::set<std::string> numbers = {"fadePercent", "flightStrength", "fps", "trailSpeedFactor", "polygonDetail", "speedContrast"};
    const std::set<std::string> booleans = {"boids", "colouredParticles", "particlesOnly", "hysteresisThresholding", "polygonGate", "objectCutout"};
    for (const auto &video : document.at("videos")) {
        if (!video.is_object() || !video.contains("source") || !video.at("source").is_string())
            throw std::runtime_error("each video requires a source string");
        for (auto it = video.begin(); it != video.end(); ++it) {
            if (it.key() == "source" || it.key() == "input") {
                if (!it.value().is_string()) throw std::runtime_error(it.key() + ": expected string");
            } else if (it.key() != "specs") throw std::runtime_error("unknown video key: " + it.key());
        }
        if (!video.contains("specs") || !video.at("specs").is_array()) throw std::runtime_error("video requires [[videos.specs]]");
        size_t specIndex = 0;
        for (const auto &spec : video.at("specs")) {
            ++specIndex;
            if (!spec.is_object()) throw std::runtime_error("spec must be a table");
            const auto context = " (video " + video.at("source").get<std::string>() + ", spec " + std::to_string(specIndex) + ")";
            std::map<std::string, std::string> boidValues;
            for (auto it = spec.begin(); it != spec.end(); ++it) {
                const auto &key = it.key();
                const auto &value = it.value();
                bool valid = false;
                if (BoidSettings::isKey(key)) {
                    if (!value.is_number()) throw std::runtime_error(key + ": expected number" + context);
                    if (key == "boidCellSize" && !value.is_number_integer())
                        throw std::runtime_error(key + ": expected integer from 8 to 512" + context);
                    boidValues.emplace(key, value.dump());
                    valid = true;
                } else if (numbers.count(key)) {
                    valid = value.is_number() && std::isfinite(value.get<double>()) && std::abs(value.get<double>()) <= std::numeric_limits<float>::max();
                    if (valid && key == "fps") valid = value.get<double>() > 0;
                    if (valid && (key == "fadePercent" || key == "trailSpeedFactor")) valid = value.get<double>() >= 0;
                    if (valid && (key == "polygonDetail" || key == "speedContrast")) valid = value.get<double>() >= 0 && value.get<double>() <= 100;
                    if (valid && key == "fadePercent") valid = value.get<double>() <= 100;
                } else if (booleans.count(key)) valid = value.is_boolean();
                else if (key == "frameOffset") valid = value.is_number_integer() && value.get<double>() >= 0 && value.get<double>() <= 120;
                else if (key == "name") valid = value.is_string();
                else if (key == "mode") valid = value.is_string() && InteractiveConfig::validMode(value.get<std::string>());
                else if (key == "treatment") valid = value.is_string() && (value == "smear" || value == "streak" || value == "stretch");
                if (!valid) throw std::runtime_error("invalid or unknown render spec field: " + key + context);
            }
            try {
                BoidSettings::parse(boidValues);
            } catch (const std::exception &error) {
                throw std::runtime_error(std::string(error.what()) + context);
            }
        }
    }
}
inline ofJson parse(const std::string &text) {
    const auto lines = narrowToml::lines(text);
    // Clearing the TOML queue starts fresh; never resurrect entries from legacy JSON.
    if (lines.empty()) return {{"version", 1}, {"videos", ofJson::array()}};
    ofJson document = {{"videos", ofJson::array()}};
    ofJson *table = &document;
    for (const auto &line : lines) {
        try {
            if (!line.table.empty()) {
                if (!line.array) throw std::runtime_error("render queue uses [[videos]] and [[videos.specs]] only");
                if (line.table == "videos") {
                    document["videos"].push_back({{"specs", ofJson::array()}});
                    table = &document["videos"].back();
                } else if (line.table == "videos.specs" && !document["videos"].empty()) {
                    auto &specs = document["videos"].back()["specs"];
                    specs.push_back(ofJson::object());
                    table = &specs.back();
                } else throw std::runtime_error("expected [[videos]] before [[videos.specs]]");
            } else {
                if (table->contains(line.key)) throw std::runtime_error("duplicate key '" + line.key + "'");
                (*table)[line.key] = scalar(line.value);
            }
        } catch (const std::exception &error) {
            throw std::runtime_error("line " + std::to_string(line.number) + ": " + error.what());
        }
    }
    validate(document);
    return document;
}
inline std::string serialize(const ofJson &document) {
    validate(document);
    std::string text = "version = 1\n";
    for (const auto &video : document.at("videos")) {
        text += "\n[[videos]]\n";
        for (auto it = video.begin(); it != video.end(); ++it)
            if (it.key() != "specs") text += it.key() + " = " + it.value().dump() + "\n";
        for (const auto &spec : video.at("specs")) {
            text += "\n[[videos.specs]]\n";
            for (auto it = spec.begin(); it != spec.end(); ++it) text += it.key() + " = " + it.value().dump() + "\n";
        }
    }
    return text;
}
inline void save(const std::filesystem::path &path, const ofJson &document) {
    const auto text = serialize(document);
    parse(text);
    const auto temporary = path.string() + ".tmp-" + std::to_string(getpid());
    try {
        narrowToml::write(temporary, text);
        std::filesystem::rename(temporary, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
inline ofJson load(const std::filesystem::path &path) {
    if (path.extension() != ".toml") throw std::runtime_error("render queue must use .toml; JSON is migration input only");
    if (!std::filesystem::exists(path)) {
        auto legacy = path;
        legacy.replace_extension(".json");
        if (std::filesystem::exists(legacy)) {
            auto document = ofJson::parse(InteractiveSettings::read(legacy.string(), 16 * 1024 * 1024));
            // Normalize the historical single-spec video shape without dropping fields.
            for (auto &video : document.at("videos")) {
                if (!video.contains("specs")) {
                    ofJson spec = video;
                    spec.erase("source"); spec.erase("input");
                    ofJson normalized = {{"source", video.at("source")}, {"specs", ofJson::array({spec})}};
                    if (video.contains("input")) normalized["input"] = video.at("input");
                    video = normalized;
                }
            }
            save(path, document);
            ofLogNotice("render") << "Migrated " << legacy << " to " << path << "; original retained";
        } else return {{"version", 1}, {"videos", ofJson::array()}};
    }
    return parse(InteractiveSettings::read(path.string(), 16 * 1024 * 1024));
}
}
