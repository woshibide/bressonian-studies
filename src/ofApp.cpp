// openFrameworks draws with OpenGL, which macOS marks deprecated but still supports.
#define GL_SILENCE_DEPRECATION

#include "ofApp.h"
#include "ParticleTuning.h"
#include "RenderQueue.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <unistd.h>

#include <OpenGL/OpenGL.h>

namespace {

using particleTuning::maxTravellers;

of::filesystem::path appDataPath(const of::filesystem::path &relativePath) {
    return (ofFilePath::getCurrentExeDirFS() / "../../../data" / relativePath)
        .lexically_normal();
}

} // namespace

void ofApp::setup() {
    processingWindow = ofGetWindowPtr();
    ofSetWindowTitle("CCTV Controls");
    ofSetVerticalSync(true);

    discoverVideos();
    loadRenderSpecs(appDataPath("render-specs.toml"));

    loadVideo(0);
    reloadSettings(true);
}

InteractiveSettings ofApp::currentSettings() const {
    InteractiveSettings settings;
    settings.frameOffset = frameOffset;
    settings.pixelFadePercent = pixelFadePercent;
    settings.pixelFlightStrength = pixelFlightStrength;
    settings.trailSpeedFactor = trailSpeedFactor;
    settings.mode = layerModeName();
    settings.treatment = pixelTreatmentName(pixelTreatment);
    settings.colouredParticles = colouredParticles;
    settings.particlesOnly = particlesOnly;
    settings.boids = boids;
    settings.boidTuning = boidTuning;
    settings.hysteresisThresholding = hysteresisThresholding;
    settings.polygonGate = polygonGate;
    settings.polygonDetail = polygonDetail;
    settings.objectCutout = objectCutout;
    settings.speedContrast = speedContrast;
    settings.useSixtyFps = useSixtyFps;
    return settings;
}

void ofApp::reloadSettings(bool force) {
    const double now = ofGetElapsedTimef();
    if (!force && now < nextSettingsPoll) return;
    nextSettingsPoll = now + 0.25;
    try {
        const std::string text = InteractiveSettings::read(appDataPath("settings.toml").string());
        if (!force && settingsObserved && text == observedSettingsText) {
            settingsPending = false;
            return;
        }
        // Require unchanged contents on consecutive polls, including atomic editor saves.
        if (!force && (!settingsPending || text != pendingSettingsText)) {
            pendingSettingsText = text;
            settingsPending = true;
            return;
        }
        observedSettingsText = text;
        settingsObserved = true;
        settingsPending = false;
        const auto config = InteractiveConfig::parse(text);
        const auto settings = config.resolve(config.selectedMode());
        interactiveConfig = config;
        sessionSettings.clear();
        applySettings(settings);
        settingsStatus = "Loaded. Live reload active; UI edits are unsaved until Save config.";
        ofLogNotice("settings") << settingsStatus;
    } catch (const std::exception &error) {
        const std::string message = std::string("ERROR: ") + error.what() +
            "\nFix bin/data/settings.toml; keeping current values.";
        if (settingsStatus != message) ofLogError("settings") << message;
        settingsStatus = message;
    }
}

void ofApp::applySettings(const InteractiveSettings &settings) {
    const bool resetHistory = frameOffset != settings.frameOffset;
    const bool resetEffect = resetHistory || layerModeName() != settings.mode ||
        pixelTreatmentName(pixelTreatment) != settings.treatment ||
        colouredParticles != settings.colouredParticles || particlesOnly != settings.particlesOnly ||
        hysteresisThresholding != settings.hysteresisThresholding ||
        polygonGate != settings.polygonGate || objectCutout != settings.objectCutout;
    // A model that learned while the cutout was off would start from a stale background.
    if (objectCutout != settings.objectCutout) resetForeground();
    frameOffset = settings.frameOffset;
    pixelFadePercent = settings.pixelFadePercent;
    pixelFlightStrength = settings.pixelFlightStrength;
    trailSpeedFactor = settings.trailSpeedFactor;
    layerMode = layerModeFromName(settings.mode);
    pixelTreatment = pixelTreatmentFromName(settings.treatment);
    colouredParticles = settings.colouredParticles;
    particlesOnly = settings.particlesOnly;
    boids = settings.boids;
    boidTuning = settings.boidTuning;
    hysteresisThresholding = settings.hysteresisThresholding;
    polygonGate = settings.polygonGate;
    polygonDetail = settings.polygonDetail;
    objectCutout = settings.objectCutout;
    speedContrast = settings.speedContrast;
    useSixtyFps = settings.useSixtyFps;
    if (resetHistory) sourceFrameHistory.clear();
    if (resetEffect) {
        hasProcessedFrame = false;
        invertedLayerInitialized = false;
        travellingPixels.clear();
    }
    editingFloatInput = FloatInput::None;
    treatmentMenuOpen = false;
    applyTargetFrameRate();
}

void ofApp::saveSettings() {
    const auto path = appDataPath("settings.toml");
    const auto temporary = path.string() + ".tmp-" + std::to_string(getpid());
    try {
        // Do not clobber an external edit that has not yet been presented to the user.
        const std::string disk = InteractiveSettings::read(path.string());
        if (!settingsObserved || settingsPending || disk != observedSettingsText) {
            settingsStatus = "Save refused: external edit pending. Wait for reload, review, then Save config.";
            return;
        }
        auto config = InteractiveConfig::parse(disk);
        config.capture(currentSettings());
        const std::string text = config.serialize();
        InteractiveConfig::parse(text);
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << text;
            output.close();
            if (!output) throw std::runtime_error("cannot write temporary settings file");
        }
        if (InteractiveSettings::read(path.string()) != disk)
            throw std::runtime_error("file changed during save; review external edit and retry");
        std::filesystem::rename(temporary, path);
        observedSettingsText = text;
        interactiveConfig = config;
        settingsStatus = "Saved current controls to bin/data/settings.toml.";
    } catch (const std::exception &error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        settingsStatus = std::string("Save failed: ") + error.what();
    }
}

void ofApp::exit() {
    if (processingWindow) {
        processingWindow->makeCurrent();
        player.close();
        processedTexture.clear();
        originalTexture.clear();
        particleCanvas.clear();
    }
    ofExit();
}

void ofApp::discoverVideos() {
    sources.clear();
    const auto directory = appDataPath("fps-normalized");
    if (!std::filesystem::is_directory(directory)) {
        renderStatus = "Video folder missing: " + directory.string();
        ofLogError("ofApp") << renderStatus;
        return;
    }

    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        const std::string extension = ofToLower(entry.path().extension().string());
        if (entry.is_regular_file() &&
            (extension == ".mp4" || extension == ".mov" || extension == ".m4v" ||
             extension == ".avi" || extension == ".mkv" || extension == ".webm")) {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    for (const auto &path : paths) {
        cv::VideoCapture capture(path.string());
        const double fps = capture.get(cv::CAP_PROP_FPS);
        if (!capture.isOpened() || !std::isfinite(fps) || fps <= 0) {
            ofLogWarning("ofApp") << "Skipping video with unreadable metadata: " << path;
            continue;
        }
        std::string label = path.stem().string();
        const std::string suffix = "-native-cadence";
        if (label.size() > suffix.size() &&
            label.compare(label.size() - suffix.size(), suffix.size(), suffix) == 0) {
            label.erase(label.size() - suffix.size());
        }
        if (!label.empty()) {
            label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        }
        sources.push_back({label, "fps-normalized/" + path.filename().string(),
                           static_cast<float>(fps)});
        ofLogNotice("ofApp") << "Found video: " << path.filename() << " at " << fps << " FPS";
    }
    if (sources.empty()) {
        renderStatus = "No readable videos in " + directory.string();
        ofLogWarning("ofApp") << renderStatus;
    }
}

bool ofApp::loadRenderSpecs(const std::filesystem::path &specsPath) {
    ofJson document;
    try { document = renderQueue::load(specsPath); }
    catch (const std::exception &error) {
        renderStatus = "Render queue error: " + std::string(error.what());
        ofLogError("render") << specsPath << ": " << renderStatus;
        return false;
    }
    renderSpecs.assign(sources.size(), {});
    selectedSpecIndices.assign(sources.size(), 0);

    for (const auto &entry : document["videos"]) {
        if (!entry.is_object() || !entry.contains("source") ||
            !entry["source"].is_string()) {
            continue;
        }
        const std::string source = entry["source"].get<std::string>();
        const auto found = std::find_if(sources.begin(), sources.end(),
                                        [&](const VideoSource &video) {
                                            return (entry.contains("input") && entry["input"].is_string())
                                                       ? video.path == entry["input"].get<std::string>()
                                                       : video.label == source;
                                        });
        if (found == sources.end()) {
            continue;
        }
        const auto index = static_cast<std::size_t>(std::distance(sources.begin(), found));
        sources[index].label = source;
        auto readSpec = [](const ofJson &sourceSpec, RenderSpec spec) {
            if (sourceSpec.contains("frameOffset") &&
                sourceSpec["frameOffset"].is_number_integer()) {
                spec.frameOffset = sourceSpec["frameOffset"].get<int>();
            }
            if (sourceSpec.contains("fadePercent") &&
                sourceSpec["fadePercent"].is_number()) {
                spec.fadePercent = sourceSpec["fadePercent"].get<float>();
            }
            if (sourceSpec.contains("flightStrength") &&
                sourceSpec["flightStrength"].is_number()) {
                spec.flightStrength = sourceSpec["flightStrength"].get<float>();
            }
            if (sourceSpec.contains("fps") && sourceSpec["fps"].is_number()) {
                spec.fps = sourceSpec["fps"].get<float>();
            }
            if (sourceSpec.contains("mode") && sourceSpec["mode"].is_string()) {
                spec.mode = sourceSpec["mode"].get<std::string>();
            }
            if (sourceSpec.contains("trailSpeedFactor") &&
                sourceSpec["trailSpeedFactor"].is_number()) {
                spec.trailSpeedFactor = sourceSpec["trailSpeedFactor"].get<float>();
            }
            if (sourceSpec.contains("treatment") && sourceSpec["treatment"].is_string()) {
                spec.treatment = pixelTreatmentName(
                    pixelTreatmentFromName(sourceSpec["treatment"].get<std::string>()));
            }
            if (sourceSpec.contains("colouredParticles") &&
                sourceSpec["colouredParticles"].is_boolean()) {
                spec.colouredParticles = sourceSpec["colouredParticles"].get<bool>();
            }
            if (sourceSpec.contains("particlesOnly") &&
                sourceSpec["particlesOnly"].is_boolean()) {
                spec.particlesOnly = sourceSpec["particlesOnly"].get<bool>();
            }
            if (sourceSpec.contains("boids") && sourceSpec["boids"].is_boolean()) {
                spec.boids = sourceSpec["boids"].get<bool>();
            }
            spec.hysteresisThresholding = sourceSpec.value("hysteresisThresholding", false);
            spec.polygonGate = sourceSpec.value("polygonGate", false);
            spec.polygonDetail = sourceSpec.value("polygonDetail", 0.0f);
            spec.objectCutout = sourceSpec.value("objectCutout", false);
            spec.speedContrast = sourceSpec.value("speedContrast", 0.0f);
            std::map<std::string, std::string> tuning;
            for (auto it = sourceSpec.begin(); it != sourceSpec.end(); ++it)
                if (BoidSettings::isKey(it.key())) tuning[it.key()] = it.value().dump();
            spec.boidTuning = BoidSettings::parse(tuning);
            return spec;
        };

        if (entry.contains("specs") && entry["specs"].is_array()) {
            for (const auto &savedSpec : entry["specs"]) {
                if (savedSpec.is_object()) {
                    renderSpecs[index].push_back(readSpec(savedSpec, RenderSpec{}));
                }
            }
        } else {
            renderSpecs[index].push_back(readSpec(entry, RenderSpec{}));
        }
        if (!renderSpecs[index].empty()) {
            selectedSpecIndices[index] = renderSpecs[index].size() - 1;
        }
    }
    return true;
}

std::size_t ofApp::savedSpecCount(std::size_t videoIndex) const {
    return videoIndex < renderSpecs.size() ? renderSpecs[videoIndex].size() : 0;
}

ofApp::LayerMode ofApp::layerModeFromName(const std::string &name) const {
    if (name == "motionMask") {
        return LayerMode::MotionMask;
    }
    if (name == "motionPolygons") {
        return LayerMode::MotionPolygons;
    }
    if (name == "displacementMap") {
        return LayerMode::DisplacementMap;
    }
    if (name == "displacedVideo") {
        return LayerMode::DisplacedVideo;
    }
    if (name == "originalVideo") {
        return LayerMode::OriginalVideo;
    }
    return LayerMode::InvertedGrayscale;
}

ofApp::PixelTreatment ofApp::pixelTreatmentFromName(const std::string &name) {
    if (name == "streak") {
        return PixelTreatment::Streak;
    }
    if (name == "stretch") {
        return PixelTreatment::Stretch;
    }
    return PixelTreatment::Smear;
}

std::string ofApp::pixelTreatmentName(PixelTreatment treatment) {
    switch (treatment) {
    case PixelTreatment::Streak:
        return "streak";
    case PixelTreatment::Stretch:
        return "stretch";
    case PixelTreatment::Smear:
    default:
        return "smear";
    }
}

std::string ofApp::layerModeName() const {
    switch (layerMode) {
    case LayerMode::MotionMask:
        return "motionMask";
    case LayerMode::MotionPolygons:
        return "motionPolygons";
    case LayerMode::DisplacementMap:
        return "displacementMap";
    case LayerMode::DisplacedVideo:
        return "displacedVideo";
    case LayerMode::OriginalVideo:
        return "originalVideo";
    case LayerMode::InvertedGrayscale:
    default:
        return "invertedGrayscale";
    }
}

bool ofApp::saveRenderSpecs() {
    if (renderSpecs.size() != sources.size() || selectedVideo >= renderSpecs.size()) {
        return false;
    }

    const auto specsPath = appDataPath("render-specs.toml");
    ofJson document;
    std::string disk;
    try {
        document = renderQueue::load(specsPath);
        if (std::filesystem::exists(specsPath)) disk = InteractiveSettings::read(specsPath.string(), 16 * 1024 * 1024);
    } catch (const std::exception &error) {
        renderStatus = "Save specs refused: " + std::string(error.what());
        return false;
    }

    RenderSpec spec;
    spec.frameOffset = frameOffset;
    spec.fadePercent = pixelFadePercent;
    spec.flightStrength = pixelFlightStrength;
    spec.trailSpeedFactor = trailSpeedFactor;
    spec.fps = 60.0f;
    spec.mode = layerModeName();
    spec.treatment = pixelTreatmentName(pixelTreatment);
    spec.colouredParticles = colouredParticles;
    spec.particlesOnly = particlesOnly;
    spec.boids = boids;
    spec.boidTuning = boidTuning;
    spec.hysteresisThresholding = hysteresisThresholding;
    spec.polygonGate = polygonGate;
    spec.polygonDetail = polygonDetail;
    spec.objectCutout = objectCutout;
    spec.speedContrast = speedContrast;
    // Append to the complete disk document, including unavailable videos and custom names.
    auto &videos = document["videos"];
    auto found = std::find_if(videos.begin(), videos.end(), [&](const ofJson &video) {
        return video.contains("input") ? video["input"] == sources[selectedVideo].path
                                       : video["source"] == sources[selectedVideo].label;
    });
    if (found == videos.end()) {
        videos.push_back({{"source", sources[selectedVideo].label},
                          {"input", sources[selectedVideo].path}, {"specs", ofJson::array()}});
        found = videos.end() - 1;
    }
    auto &specs = (*found)["specs"];
    specs.push_back({
        {"name", "Spec " + ofToString(specs.size() + 1, 2, '0')},
        {"fps", spec.fps}, {"mode", spec.mode}, {"frameOffset", spec.frameOffset},
        {"fadePercent", spec.fadePercent}, {"flightStrength", spec.flightStrength},
        {"trailSpeedFactor", spec.trailSpeedFactor}, {"treatment", spec.treatment},
        {"colouredParticles", spec.colouredParticles}, {"particlesOnly", spec.particlesOnly},
        {"boids", spec.boids}, {"hysteresisThresholding", spec.hysteresisThresholding},
        {"polygonGate", spec.polygonGate}, {"polygonDetail", spec.polygonDetail},
        {"objectCutout", spec.objectCutout}, {"speedContrast", spec.speedContrast},
        {"boidCellSize", spec.boidTuning.boidCellSize},
        {"boidAlignment", spec.boidTuning.boidAlignment},
        {"boidCohesion", spec.boidTuning.boidCohesion},
        {"boidSeparation", spec.boidTuning.boidSeparation},
        {"boidCrowdCount", spec.boidTuning.boidCrowdCount},
        {"boidSwirl", spec.boidTuning.boidSwirl},
        {"boidSwirlDirection", spec.boidTuning.boidSwirlDirection}
    });
    try {
        if (std::filesystem::exists(specsPath) ? InteractiveSettings::read(specsPath.string(), 16 * 1024 * 1024) != disk : !disk.empty())
            throw std::runtime_error("queue changed during save; retry");
        renderQueue::save(specsPath, document);
    } catch (const std::exception &error) {
        renderStatus = "Save specs failed: " + std::string(error.what());
        return false;
    }
    if (!loadRenderSpecs(specsPath)) return false;
    renderStatus = "Saved spec " + ofToString(renderSpecs[selectedVideo].size()) +
                   " for " + sources[selectedVideo].label;
    return true;
}

void ofApp::loadVideo(std::size_t index) {
    if (index >= sources.size()) {
        return;
    }

    player.stop();
    player.close();
    latestVideoFrame.clear();
    sourceFrameHistory.clear();
    motionMaskPixels.clear();
    accumulatedInvertedFrame.clear();
    displacementMapPixels.clear();
    displacedVideoPixels.clear();
    displacementField.release();
    travellingPixels.clear();
    resetForeground();
    hasLatestVideoFrame = false;
    hasProcessedFrame = false;
    invertedLayerInitialized = false;
    selectedVideo = index;
    // Interactive settings persist across videos; the TOML queue is only for renders.
    selectedSpecIndices[index] = renderSpecs[index].empty() ? 0 : renderSpecs[index].size() - 1;
    originalTexture.clear();
    applyTargetFrameRate();

    player.setPixelFormat(OF_PIXELS_RGB);
    if (!player.load(appDataPath(sources[selectedVideo].path))) {
        ofLogError("ofApp") << "Could not load video: " << sources[selectedVideo].path;
        return;
    }

    player.setLoopState(OF_LOOP_NORMAL);
    player.play();
}

float ofApp::targetFrameRate(std::size_t index) const {
    return useSixtyFps ? 60.0f : sources[index].averageFps;
}

void ofApp::applyTargetFrameRate() {
    if (sources.empty()) return;
    ofSetFrameRate(static_cast<int>(std::lround(targetFrameRate(selectedVideo))));
    motionSpreadTicks = std::max(
        1, static_cast<int>(std::lround(targetFrameRate(selectedVideo) /
                                        sources[selectedVideo].averageFps)));
}

float ofApp::fadeFactor() const {
    return 1.0f - pixelFadePercent / 100.0f;
}

void ofApp::update() {
    // Only this window owns playback, simulation, uploads and ParticleCanvas FBOs.
    processingWindow->makeCurrent();
    reloadSettings();
    player.update();
    const bool sourceFrameChanged = player.isLoaded() && player.isFrameNew();
    if (sourceFrameChanged) {
        const ofPixels &pixels = player.getPixels();
        if (!pixels.isAllocated() || pixels.getNumChannels() != 3) {
            return;
        }
        latestVideoFrame = pixels;
        originalTexture.loadData(pixels);
        glFinish(); // Publish uploads before the other context samples the shared texture.
        hasLatestVideoFrame = true;
    }

    if (!hasLatestVideoFrame || (!useSixtyFps && !sourceFrameChanged)) {
        return;
    }

    const ofPixels &pixels = latestVideoFrame;
    if (sourceFrameChanged) {
        pushSourceFrame(pixels);
    }

    if (layerMode == LayerMode::MotionMask || layerMode == LayerMode::MotionPolygons) {
        if (updateMotionFrame()) {
            uploadProcessedPixels(motionMaskPixels);
        }
    } else if (layerMode != LayerMode::OriginalVideo) {
        updateInvertedFrame(pixels, sourceFrameChanged);
        uploadSelectedInvertedLayer();
    }
}

void ofApp::pushSourceFrame(const ofPixels &pixels) {
    // Every layer counts the frame offset in source frames, not output frames.
    sourceFrameHistory.push_back(pixels);
    while (sourceFrameHistory.size() > static_cast<std::size_t>(frameOffset + 1)) {
        sourceFrameHistory.pop_front();
    }
    // The model learns once per source frame, so repeated output frames do not skew it.
    if (objectCutout) updateForeground(pixels);
}

void ofApp::updateForeground(const ofPixels &pixels) {
    const int width = static_cast<int>(pixels.getWidth());
    const int height = static_cast<int>(pixels.getHeight());
    cv::Mat frame(height, width, CV_8UC3, const_cast<unsigned char *>(pixels.getData()));
    // Model at most 960 pixels wide for speed; the mask is scaled back to source size.
    const double scale = std::min(1.0, 960.0 / width);
    cv::Mat modelFrame = frame;
    if (scale < 1.0) cv::resize(frame, modelFrame, cv::Size(), scale, scale, cv::INTER_AREA);
    if (!backgroundModel) {
        // OpenCV defaults: 500 frames of history, variance threshold 16, shadows marked 127.
        backgroundModel = cv::createBackgroundSubtractorMOG2(500, 16.0, true);
    }
    cv::Mat modelMask;
    backgroundModel->apply(modelFrame, modelMask);
    cv::resize(modelMask, foregroundMask, frame.size(), 0, 0, cv::INTER_NEAREST);
}

void ofApp::resetForeground() {
    backgroundModel.release();
    foregroundMask.release();
}

cv::Mat ofApp::thresholdMotion(const cv::Mat &grayscaleDifference) const {
    constexpr int highThreshold = 24;
    cv::Mat strong;
    cv::threshold(grayscaleDifference, strong, highThreshold, 255, cv::THRESH_BINARY);
    if (!hysteresisThresholding) return strong;

    constexpr int lowThreshold = 12;
    cv::Mat weak;
    cv::threshold(grayscaleDifference, weak, lowThreshold, 255, cv::THRESH_BINARY);
    cv::Mat labels;
    // Match scikit-image's default 2D connectivity: shared edges, not diagonals.
    const int componentCount = cv::connectedComponents(weak, labels, 4, CV_32S);
    std::vector<unsigned char> seeded(componentCount, 0);
    for (int y = 0; y < strong.rows; ++y) {
        const auto *strongRow = strong.ptr<unsigned char>(y);
        const auto *labelRow = labels.ptr<int>(y);
        for (int x = 0; x < strong.cols; ++x) {
            if (strongRow[x]) seeded[labelRow[x]] = 255;
        }
    }
    cv::Mat result(strong.size(), CV_8UC1);
    for (int y = 0; y < result.rows; ++y) {
        auto *outputRow = result.ptr<unsigned char>(y);
        const auto *labelRow = labels.ptr<int>(y);
        for (int x = 0; x < result.cols; ++x) outputRow[x] = seeded[labelRow[x]];
    }
    return result;
}

std::vector<std::vector<cv::Point>> ofApp::traceMotionPolygons(const cv::Mat &motionMask) const {
    // Detail 0 uses the coarse values below; 100 scales them to zero.
    const double coarseness = 1.0 - std::clamp(polygonDetail, 0.0f, 100.0f) / 100.0;
    // Close small gaps so fragments of one moving body merge before their outline is traced.
    const int closeSize = static_cast<int>(std::lround(motionMask.cols / 60.0 * coarseness)) | 1;
    cv::Mat closed = motionMask;
    if (closeSize > 1) {
        cv::morphologyEx(motionMask, closed, cv::MORPH_CLOSE,
                         cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(closeSize, closeSize)));
    }
    std::vector<std::vector<cv::Point>> contours;
    // Outer outlines only, so holes inside a moving region are filled with it.
    cv::findContours(closed, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    // Regions under 0.05% of the frame are noise, not moving bodies.
    const double minArea = motionMask.total() * 0.0005 * coarseness;
    std::vector<std::vector<cv::Point>> polygons;
    for (const auto &contour : contours) {
        if (cv::contourArea(contour) < minArea) continue;
        std::vector<cv::Point> polygon;
        // Straight edges that stay within 1.5% of the outline's length from it.
        cv::approxPolyDP(contour, polygon,
                         0.015 * coarseness * cv::arcLength(contour, true), true);
        polygons.push_back(std::move(polygon));
    }
    return polygons;
}

cv::Mat ofApp::motionGate(const cv::Mat &grayscaleDifference) const {
    cv::Mat gate = thresholdMotion(grayscaleDifference);
    if (polygonGate) {
        const auto polygons = traceMotionPolygons(gate);
        gate.setTo(0);
        if (!polygons.empty()) cv::fillPoly(gate, polygons, cv::Scalar(255));
    }
    if (objectCutout && foregroundMask.size() == gate.size()) {
        // The difference covers the object then and now plus its shadow; the model keeps
        // only the object as it is in the current frame.
        cv::bitwise_and(gate, foregroundMask == 255, gate);
    }
    return gate;
}

bool ofApp::updateMotionFrame() {
    if (sourceFrameHistory.size() <= static_cast<std::size_t>(frameOffset)) {
        if (!sourceFrameHistory.empty()) {
            const ofPixels &current = sourceFrameHistory.back();
            const int width = static_cast<int>(current.getWidth());
            const int height = static_cast<int>(current.getHeight());
            if (!motionMaskPixels.isAllocated() || motionMaskPixels.getWidth() != width ||
                motionMaskPixels.getHeight() != height) {
                motionMaskPixels.allocate(width, height, OF_PIXELS_RGB);
            }
            std::memset(motionMaskPixels.getData(), 0, motionMaskPixels.size());
        }
        return false;
    }

    const ofPixels &current = sourceFrameHistory.back();
    const ofPixels &delayed = sourceFrameHistory[sourceFrameHistory.size() - 1 - frameOffset];
    const int width = static_cast<int>(current.getWidth());
    const int height = static_cast<int>(current.getHeight());

    cv::Mat currentFrame(height, width, CV_8UC3,
                         const_cast<unsigned char *>(current.getData()));
    cv::Mat delayedFrame(height, width, CV_8UC3,
                         const_cast<unsigned char *>(delayed.getData()));
    cv::Mat difference;
    cv::Mat grayscaleDifference;
    cv::Mat layer;
    cv::absdiff(currentFrame, delayedFrame, difference);
    cv::cvtColor(difference, grayscaleDifference, cv::COLOR_RGB2GRAY);
    if (layerMode == LayerMode::MotionPolygons) {
        // Dimmed source, thresholded pixels in grey and their polygons on top. The polygons
        // are filled only while the polygon gate feeds them to the effects. With the object
        // cutout, the gated region instead shows the object in source colour and its shadow
        // in violet.
        const cv::Mat motionMask = thresholdMotion(grayscaleDifference);
        const auto polygons = traceMotionPolygons(motionMask);
        const cv::Scalar outline = polygonGate ? cv::Scalar(255, 190, 40) : cv::Scalar(90, 200, 255);
        const int thickness = std::max(1, width / 480);
        const bool showCutout = objectCutout && foregroundMask.size() == motionMask.size();
        currentFrame.convertTo(layer, -1, 0.35);
        if (showCutout) {
            cv::Mat region = motionMask;
            if (polygonGate) {
                region = cv::Mat::zeros(motionMask.size(), CV_8UC1);
                if (!polygons.empty()) cv::fillPoly(region, polygons, cv::Scalar(255));
            }
            currentFrame.copyTo(layer, region & (foregroundMask == 255));
            layer.setTo(cv::Scalar(170, 90, 255), region & (foregroundMask == 127));
        } else {
            layer.setTo(cv::Scalar(150, 150, 150), motionMask);
        }
        if (polygonGate && !showCutout && !polygons.empty()) {
            cv::Mat filled = layer.clone();
            cv::fillPoly(filled, polygons, outline);
            cv::addWeighted(filled, 0.3, layer, 0.7, 0.0, layer);
        }
        cv::polylines(layer, polygons, true, outline, thickness, cv::LINE_AA);
        for (const auto &polygon : polygons) {
            // Dots mark corners of coarse polygons; on detailed outlines they would merge into blobs.
            if (cv::arcLength(polygon, true) < polygon.size() * thickness * 8.0) continue;
            for (const auto &vertex : polygon) {
                cv::circle(layer, vertex, thickness * 2, outline, cv::FILLED, cv::LINE_AA);
            }
        }
    } else {
        cv::cvtColor(motionGate(grayscaleDifference), layer, cv::COLOR_GRAY2RGB);
    }

    ofPixels output;
    output.allocate(width, height, OF_PIXELS_RGB);
    std::memcpy(output.getData(), layer.data, layer.total() * layer.elemSize());
    motionMaskPixels = output;
    return true;
}

void ofApp::updateInvertedFrame(const ofPixels &pixels, bool sourceFrameChanged) {
    const int width = static_cast<int>(pixels.getWidth());
    const int height = static_cast<int>(pixels.getHeight());
    cv::Mat currentFrame(height, width, CV_8UC3,
                         const_cast<unsigned char *>(pixels.getData()));
    cv::Mat grayscale;
    cv::cvtColor(currentFrame, grayscale, cv::COLOR_RGB2GRAY);

    if (!invertedLayerInitialized || !accumulatedInvertedFrame.isAllocated() ||
        accumulatedInvertedFrame.getWidth() != width ||
        accumulatedInvertedFrame.getHeight() != height ||
        displacementField.cols != width || displacementField.rows != height) {
        accumulatedInvertedFrame.allocate(width, height, OF_PIXELS_RGB);
        std::memset(accumulatedInvertedFrame.getData(), 0,
                    accumulatedInvertedFrame.size());
        displacementMapPixels.allocate(width, height, OF_PIXELS_RGB);
        displacedVideoPixels.allocate(width, height, OF_PIXELS_RGB);
        displacementField = cv::Mat::zeros(height, width, CV_32FC2);
        for (int y = 0; y < height; ++y) {
            unsigned char *mapRow = displacementMapPixels.getData() +
                                    static_cast<std::size_t>(y) * width * 3;
            for (int x = 0; x < width; ++x) {
                mapRow[x * 3] = 128;
                mapRow[x * 3 + 1] = 128;
                mapRow[x * 3 + 2] = 0;
            }
        }
        if (colouredParticles && particlesOnly) {
            std::memset(displacedVideoPixels.getData(), 0, displacedVideoPixels.size());
        } else {
            std::memcpy(displacedVideoPixels.getData(), pixels.getData(), pixels.size());
        }
        travellingPixels.clear();
        pendingFlow.release();
        pendingMovingPixels.release();
        pendingTravellers.clear();
        invertedLayerInitialized = true;
        return;
    }

    // Motion is measured across frameOffset source frames, so an offset of 0 has no
    // motion: every layer shows its rest state and nothing keeps travelling.
    if (frameOffset == 0) {
        displacementField.setTo(cv::Scalar::all(0));
        cv::Mat(height, width, CV_8UC3, displacementMapPixels.getData())
            .setTo(cv::Scalar(128, 128, 0));
        if (colouredParticles && particlesOnly) {
            std::memset(displacedVideoPixels.getData(), 0, displacedVideoPixels.size());
        } else {
            std::memcpy(displacedVideoPixels.getData(), pixels.getData(), pixels.size());
        }
        std::memset(accumulatedInvertedFrame.getData(), 0, accumulatedInvertedFrame.size());
        travellingPixels.clear();
        pendingFlow.release();
        pendingMovingPixels.release();
        pendingTravellers.clear();
        return;
    }

    const bool motionMeasured =
        sourceFrameChanged &&
        sourceFrameHistory.size() == static_cast<std::size_t>(frameOffset + 1);
    cv::Mat flow;
    cv::Mat movingPixels;
    if (motionMeasured) {
        const ofPixels &offsetFrame = sourceFrameHistory.front();
        cv::Mat previousFrame(height, width, CV_8UC3,
                              const_cast<unsigned char *>(offsetFrame.getData()));
        cv::Mat previousGrayscale;
        cv::Mat previousFlowFrame;
        cv::Mat currentFlowFrame;
        cv::Mat lowResolutionFlow;
        cv::Mat difference;
        cv::Mat grayscaleDifference;
        cv::absdiff(currentFrame, previousFrame, difference);
        cv::cvtColor(difference, grayscaleDifference, cv::COLOR_RGB2GRAY);
        movingPixels = motionGate(grayscaleDifference);
        cv::cvtColor(previousFrame, previousGrayscale, cv::COLOR_RGB2GRAY);
        const cv::Size flowSize(std::max(1, width / 2), std::max(1, height / 2));
        cv::resize(previousGrayscale, previousFlowFrame, flowSize, 0, 0, cv::INTER_AREA);
        cv::resize(grayscale, currentFlowFrame, flowSize, 0, 0, cv::INTER_AREA);
        cv::calcOpticalFlowFarneback(previousFlowFrame, currentFlowFrame, lowResolutionFlow,
                                     0.5, 3, 15, 3, 5, 1.2, 0);
        cv::resize(lowResolutionFlow, flow, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);
        flow *= 2.0f;
        pendingFlow = flow;
        pendingMovingPixels = movingPixels;
        motionSpreadTick = 0;
    }
    const bool injectMotion = !pendingFlow.empty() && motionSpreadTick < motionSpreadTicks;
    const float injectWeight = 1.0f / motionSpreadTicks;

    const bool pushPixels = colouredParticles && (layerMode == LayerMode::DisplacementMap ||
                                                  layerMode == LayerMode::DisplacedVideo);
    if (!pushPixels) {
        const float fadeFactor = this->fadeFactor();
        cv::Mat mapX(height, width, CV_32FC1);
        cv::Mat mapY(height, width, CV_32FC1);
        constexpr float maxMapOffset = 127.0f;
        // Rows are independent, so the field update runs across all cores.
        cv::parallel_for_(cv::Range(0, height), [&](const cv::Range &rows) {
            for (int y = rows.start; y < rows.end; ++y) {
                cv::Vec2f *fieldRow = displacementField.ptr<cv::Vec2f>(y);
                float *mapXRow = mapX.ptr<float>(y);
                float *mapYRow = mapY.ptr<float>(y);
                unsigned char *encodedMapRow = displacementMapPixels.getData() +
                                               static_cast<std::size_t>(y) * width * 3;
                const cv::Vec2f *flowRow =
                    injectMotion ? pendingFlow.ptr<cv::Vec2f>(y) : nullptr;
                const unsigned char *movingRow =
                    injectMotion ? pendingMovingPixels.ptr<unsigned char>(y) : nullptr;
                for (int x = 0; x < width; ++x) {
                    cv::Vec2f &offset = fieldRow[x];
                    offset[0] *= fadeFactor;
                    offset[1] *= fadeFactor;
                    if (injectMotion && movingRow[x] != 0) {
                        const cv::Vec2f movement = flowRow[x];
                        if (std::isfinite(movement[0]) && std::isfinite(movement[1])) {
                            offset[0] += movement[0] * pixelFlightStrength * injectWeight;
                            offset[1] += movement[1] * pixelFlightStrength * injectWeight;
                        }
                    }
                    offset[0] = std::clamp(offset[0], -maxMapOffset, maxMapOffset);
                    offset[1] = std::clamp(offset[1], -maxMapOffset, maxMapOffset);

                    mapXRow[x] = x - offset[0];
                    mapYRow[x] = y - offset[1];
                    encodedMapRow[x * 3] = static_cast<unsigned char>(
                        std::lround(offset[0] + 128.0f));
                    encodedMapRow[x * 3 + 1] = static_cast<unsigned char>(
                        std::lround(offset[1] + 128.0f));
                    encodedMapRow[x * 3 + 2] = static_cast<unsigned char>(
                        std::min(255.0f, std::max(std::abs(offset[0]), std::abs(offset[1])) * 2.0f));
                }
            }
        });
        cv::Mat displacedFrame(height, width, CV_8UC3, displacedVideoPixels.getData());
        if (pixelTreatment == PixelTreatment::Smear) {
            // Warp the previous output again and blend it in, so moved pixels drag paint behind them.
            cv::Mat previousDisplacedFrame = displacedFrame.clone();
            cv::Mat warpedCurrentFrame;
            cv::Mat warpedFeedbackFrame;
            cv::remap(currentFrame, warpedCurrentFrame, mapX, mapY, cv::INTER_LINEAR,
                      cv::BORDER_REPLICATE);
            cv::remap(previousDisplacedFrame, warpedFeedbackFrame, mapX, mapY,
                      cv::INTER_LINEAR, cv::BORDER_REPLICATE);
            const float feedbackWeight = std::clamp(fadeFactor, 0.0f, 0.98f);
            cv::addWeighted(warpedCurrentFrame, 1.0f - feedbackWeight,
                            warpedFeedbackFrame, feedbackWeight, 0.0, displacedFrame);
        } else if (pixelTreatment == PixelTreatment::Streak) {
            // Average the current frame along each pixel's displacement path, which draws
            // straight lines from where a pixel rests to where it was pulled from.
            constexpr int streakSamples = 12;
            cv::Mat streakSum = cv::Mat::zeros(height, width, CV_32FC3);
            cv::Mat sampleX(height, width, CV_32FC1);
            cv::Mat sampleY(height, width, CV_32FC1);
            cv::Mat sample;
            for (int sampleIndex = 0; sampleIndex < streakSamples; ++sampleIndex) {
                const float pathPosition = static_cast<float>(sampleIndex) / (streakSamples - 1);
                cv::parallel_for_(cv::Range(0, height), [&](const cv::Range &rows) {
                    for (int y = rows.start; y < rows.end; ++y) {
                        const cv::Vec2f *fieldRow = displacementField.ptr<cv::Vec2f>(y);
                        float *sampleXRow = sampleX.ptr<float>(y);
                        float *sampleYRow = sampleY.ptr<float>(y);
                        for (int x = 0; x < width; ++x) {
                            sampleXRow[x] = x - fieldRow[x][0] * pathPosition;
                            sampleYRow[x] = y - fieldRow[x][1] * pathPosition;
                        }
                    }
                });
                cv::remap(currentFrame, sample, sampleX, sampleY, cv::INTER_LINEAR,
                          cv::BORDER_REPLICATE);
                cv::accumulate(sample, streakSum);
            }
            streakSum.convertTo(displacedFrame, CV_8UC3, 1.0 / streakSamples);
        } else {
            // Warp only the current frame, so pixels behind motion repeat and the image stretches.
            cv::remap(currentFrame, displacedFrame, mapX, mapY, cv::INTER_LINEAR,
                      cv::BORDER_REPLICATE);
        }
    }

    // The traveller simulation only runs for layers that draw it.
    if (layerMode == LayerMode::InvertedGrayscale || pushPixels) {
        updateTravellingPixels(grayscale, currentFrame, motionMeasured, injectMotion);
    }
    if (injectMotion) {
        ++motionSpreadTick;
    }
}

void ofApp::updateTravellingPixels(const cv::Mat &grayscale, const cv::Mat &currentFrame,
                                   bool motionMeasured, bool injectMotion) {
    const cv::Mat &flow = pendingFlow;
    const cv::Mat &movingPixels = pendingMovingPixels;
    const int width = grayscale.cols;
    const int height = grayscale.rows;
    // Grayscale traces add inverted brightness; coloured particles carry the source colour.
    const bool colourMode = layerMode != LayerMode::InvertedGrayscale;
    const bool drawMap = layerMode == LayerMode::DisplacementMap;
    cv::Mat inverted;
    cv::bitwise_not(grayscale, inverted);
    cv::Mat target;
    const float fadeFactor = this->fadeFactor();

    if (!colourMode) {
        target = cv::Mat(height, width, CV_8UC3, accumulatedInvertedFrame.getData());
        if (fadeFactor < 1.0f) {
            target.convertTo(target, CV_8UC3, fadeFactor);
        }
    } else if (drawMap) {
        target = cv::Mat(height, width, CV_8UC3, displacementMapPixels.getData());
        target.setTo(cv::Scalar(128, 128, 0));
    } else {
        target = cv::Mat(height, width, CV_8UC3, displacedVideoPixels.getData());
        // Particles only replaces the current frame with black, so no source pixels show.
        const cv::Mat background =
            particlesOnly ? cv::Mat::zeros(height, width, CV_8UC3) : currentFrame;
        if (pixelTreatment == PixelTreatment::Smear) {
            // Old paint fades toward the background; particles keep painting over it.
            const float feedbackWeight = std::clamp(fadeFactor, 0.0f, 0.98f);
            cv::addWeighted(background, 1.0f - feedbackWeight, target, feedbackWeight, 0.0,
                            target);
        } else {
            background.copyTo(target);
        }
    }

    // The caller skips the simulation at offset 0, so the half-life is always positive.
    const float halfLifeFrames = static_cast<float>(frameOffset);
    const float speedDecay = std::pow(0.5f, 1.0f / halfLifeFrames);
    constexpr float stopSpeed = particleTuning::stopSpeed;
    constexpr float maxTrailLength = 48.0f;
    constexpr float maxStretchLength = 256.0f;
    constexpr float maxMapOffset = 127.0f;

    // Coloured smear paints only the head; every other mode draws a line from the head back.
    const bool drawPoints = colourMode && pixelTreatment == PixelTreatment::Smear;
    const std::size_t verticesPerTraveller = drawPoints ? 1 : 2;
    auto alphaFromStrength = [](float strength) {
        return static_cast<unsigned char>(std::lround(std::clamp(strength, 0.0f, 1.0f) * 255.0f));
    };
    auto streakLength = [&](const TravellingPixel &traveller) {
        const float baseTrailLength = std::min(maxTrailLength,
                                               traveller.initialSpeed * halfLifeFrames);
        const float speedAdjustment = std::exp(-trailSpeedFactor * traveller.initialSpeed);
        return std::clamp(baseTrailLength * speedAdjustment, 0.0f, maxTrailLength);
    };

    // Writes the traveller's point, or the head and tail of its line. Faded lines weaken
    // toward the tail; the GPU interpolates colour and strength between the two ends.
    auto writeVertices = [&](const TravellingPixel &traveller, ParticleCanvas::Vertex *out) {
        cv::Vec3b colour(traveller.brightness, traveller.brightness, traveller.brightness);
        float backX = -traveller.directionX;
        float backY = -traveller.directionY;
        float length = 0.0f;
        bool faded = true;
        if (!colourMode) {
            length = streakLength(traveller);
        } else {
            colour = traveller.colour;
            if (speedContrast > 0.0f) {
                // Contrast stretch: faster particles push each channel further from mid grey,
                // compressing their colours toward the extremes, and relax as they slow.
                const float speed = std::hypot(traveller.velocityX, traveller.velocityY) *
                                    std::abs(pixelFlightStrength);
                const float gain = 1.0f + speedContrast * speed;
                for (int channel = 0; channel < 3; ++channel) {
                    colour[channel] =
                        cv::saturate_cast<unsigned char>((colour[channel] - 128.0f) * gain + 128.0f);
                }
            }
            if (drawMap) {
                const float offsetX = std::clamp(traveller.x - traveller.originX,
                                                 -maxMapOffset, maxMapOffset);
                const float offsetY = std::clamp(traveller.y - traveller.originY,
                                                 -maxMapOffset, maxMapOffset);
                colour = cv::Vec3b(
                    static_cast<unsigned char>(std::lround(offsetX + 128.0f)),
                    static_cast<unsigned char>(std::lround(offsetY + 128.0f)),
                    static_cast<unsigned char>(std::min(
                        255.0f, std::max(std::abs(offsetX), std::abs(offsetY)) * 2.0f)));
            }
            if (pixelTreatment == PixelTreatment::Streak) {
                length = streakLength(traveller);
            } else if (pixelTreatment == PixelTreatment::Stretch) {
                // Stretch draws the pixel at full strength along its whole path from its origin.
                faded = false;
                const float pathX = traveller.originX - traveller.x;
                const float pathY = traveller.originY - traveller.y;
                const float distance = std::hypot(pathX, pathY);
                if (distance >= 0.5f && std::isfinite(distance)) {
                    backX = pathX / distance;
                    backY = pathY / distance;
                    length = std::min(distance, maxStretchLength);
                }
            }
        }

        out[0] = {traveller.x, traveller.y, colour[0], colour[1], colour[2],
                  alphaFromStrength(traveller.opacity)};
        if (verticesPerTraveller == 2) {
            // The GPU skips zero-length lines, so each line covers at least one pixel.
            length = std::max(length, 1.0f);
            const float tailStrength =
                traveller.opacity * (faded ? 1.0f - length / (length + 1.0f) : 1.0f);
            out[1] = {traveller.x + backX * length, traveller.y + backY * length, colour[0],
                      colour[1], colour[2], alphaFromStrength(tailStrength)};
        }
    };

    // Boids find neighbours through a grid: each cell sums the travellers inside it, and each
    // neighbourhood sums its cell and the eight around it.
    struct BoidCell {
        double count = 0.0;
        double sumX = 0.0;
        double sumY = 0.0;
        double headingX = 0.0;
        double headingY = 0.0;
    };
    const int cellSize = boidTuning.boidCellSize;
    const int gridColumns = (width + cellSize - 1) / cellSize;
    const int gridRows = (height + cellSize - 1) / cellSize;
    auto cellIndex = [&](float x, float y) {
        const int column = std::clamp(static_cast<int>(x) / cellSize, 0, gridColumns - 1);
        const int row = std::clamp(static_cast<int>(y) / cellSize, 0, gridRows - 1);
        return static_cast<std::size_t>(row * gridColumns + column);
    };
    // Steering acts on the direction travellers actually move, which flips with negative flight.
    const float movementSign = pixelFlightStrength < 0.0f ? -1.0f : 1.0f;
    const bool steerBoids = boids && !travellingPixels.empty();
    std::vector<BoidCell> cells;
    std::vector<BoidCell> neighbourhoods;
    if (steerBoids) {
        // Fixed stripes summed in a fixed order keep renders identical from run to run.
        constexpr int stripeCount = 16;
        const std::size_t cellCount = static_cast<std::size_t>(gridColumns * gridRows);
        std::vector<std::vector<BoidCell>> stripeCells(stripeCount,
                                                       std::vector<BoidCell>(cellCount));
        const std::size_t stripeLength =
            (travellingPixels.size() + stripeCount - 1) / stripeCount;
        cv::parallel_for_(cv::Range(0, stripeCount), [&](const cv::Range &range) {
            for (int stripe = range.start; stripe < range.end; ++stripe) {
                std::vector<BoidCell> &grid = stripeCells[stripe];
                const std::size_t first = stripe * stripeLength;
                const std::size_t last =
                    std::min(travellingPixels.size(), first + stripeLength);
                for (std::size_t index = first; index < last; ++index) {
                    const TravellingPixel &traveller = travellingPixels[index];
                    BoidCell &cell = grid[cellIndex(traveller.x, traveller.y)];
                    cell.count += 1.0;
                    cell.sumX += traveller.x;
                    cell.sumY += traveller.y;
                    cell.headingX += traveller.directionX * movementSign;
                    cell.headingY += traveller.directionY * movementSign;
                }
            }
        });
        cells = std::move(stripeCells[0]);
        for (int stripe = 1; stripe < stripeCount; ++stripe) {
            for (std::size_t index = 0; index < cellCount; ++index) {
                const BoidCell &part = stripeCells[stripe][index];
                BoidCell &cell = cells[index];
                cell.count += part.count;
                cell.sumX += part.sumX;
                cell.sumY += part.sumY;
                cell.headingX += part.headingX;
                cell.headingY += part.headingY;
            }
        }
        neighbourhoods.resize(cellCount);
        for (int row = 0; row < gridRows; ++row) {
            for (int column = 0; column < gridColumns; ++column) {
                BoidCell &neighbourhood = neighbourhoods[row * gridColumns + column];
                for (int nearRow = std::max(0, row - 1);
                     nearRow <= std::min(gridRows - 1, row + 1); ++nearRow) {
                    for (int nearColumn = std::max(0, column - 1);
                         nearColumn <= std::min(gridColumns - 1, column + 1); ++nearColumn) {
                        const BoidCell &cell = cells[nearRow * gridColumns + nearColumn];
                        neighbourhood.count += cell.count;
                        neighbourhood.sumX += cell.sumX;
                        neighbourhood.sumY += cell.sumY;
                        neighbourhood.headingX += cell.headingX;
                        neighbourhood.headingY += cell.headingY;
                    }
                }
            }
        }
    }

    // Blends the four nearest cell centres, so steering changes smoothly instead of per cell.
    auto sampleGrid = [&](const std::vector<BoidCell> &grid, float x, float y) {
        const float gridX = std::clamp(x / cellSize - 0.5f, 0.0f, gridColumns - 1.0f);
        const float gridY = std::clamp(y / cellSize - 0.5f, 0.0f, gridRows - 1.0f);
        const int left = static_cast<int>(gridX);
        const int top = static_cast<int>(gridY);
        const int right = std::min(left + 1, gridColumns - 1);
        const int bottom = std::min(top + 1, gridRows - 1);
        const double blendX = gridX - left;
        const double blendY = gridY - top;
        BoidCell sample;
        auto add = [&](int column, int row, double weight) {
            const BoidCell &cell = grid[row * gridColumns + column];
            sample.count += cell.count * weight;
            sample.sumX += cell.sumX * weight;
            sample.sumY += cell.sumY * weight;
            sample.headingX += cell.headingX * weight;
            sample.headingY += cell.headingY * weight;
        };
        add(left, top, (1.0 - blendX) * (1.0 - blendY));
        add(right, top, blendX * (1.0 - blendY));
        add(left, bottom, (1.0 - blendX) * blendY);
        add(right, bottom, blendX * blendY);
        return sample;
    };

    // Turns a traveller toward its flock and around the flock's centre without changing speed.
    auto steer = [&](TravellingPixel &traveller) {
        const BoidCell neighbourhood = sampleGrid(neighbourhoods, traveller.x, traveller.y);
        const float speed = std::hypot(traveller.velocityX, traveller.velocityY);
        if (neighbourhood.count < 2.0 || !(speed > 0.0f)) {
            return;
        }
        const float headingX = traveller.directionX * movementSign;
        const float headingY = traveller.directionY * movementSign;
        float turnX = 0.0f;
        float turnY = 0.0f;

        const float flockHeadingLength = static_cast<float>(
            std::hypot(neighbourhood.headingX, neighbourhood.headingY));
        if (flockHeadingLength > 1e-6f) {
            turnX += boidTuning.boidAlignment *
                     (static_cast<float>(neighbourhood.headingX) / flockHeadingLength - headingX);
            turnY += boidTuning.boidAlignment *
                     (static_cast<float>(neighbourhood.headingY) / flockHeadingLength - headingY);
        }

        const float toCentreX =
            static_cast<float>(neighbourhood.sumX / neighbourhood.count) - traveller.x;
        const float toCentreY =
            static_cast<float>(neighbourhood.sumY / neighbourhood.count) - traveller.y;
        const float centreDistance = std::hypot(toCentreX, toCentreY);
        if (centreDistance > 1e-3f) {
            const float pull = boidTuning.boidCohesion *
                               std::min(centreDistance / static_cast<float>(cellSize), 1.0f);
            turnX += pull * toCentreX / centreDistance;
            turnY += pull * toCentreY / centreDistance;
            // Clockwise on screen, where y points down.
            const float swirl = boidTuning.boidSwirl * boidTuning.boidSwirlDirection;
            turnX += swirl * toCentreY / centreDistance;
            turnY -= swirl * toCentreX / centreDistance;
        }

        const BoidCell cell = sampleGrid(cells, traveller.x, traveller.y);
        const float crowding = std::clamp(
            static_cast<float>(cell.count) / boidTuning.boidCrowdCount - 1.0f, 0.0f, 1.0f);
        if (crowding > 0.0f && cell.count > 0.0) {
            const float awayX = traveller.x - static_cast<float>(cell.sumX / cell.count);
            const float awayY = traveller.y - static_cast<float>(cell.sumY / cell.count);
            const float awayDistance = std::hypot(awayX, awayY);
            if (awayDistance > 1e-3f) {
                const float push = boidTuning.boidSeparation * crowding;
                turnX += push * awayX / awayDistance;
                turnY += push * awayY / awayDistance;
            }
        }

        const float newHeadingX = headingX + turnX;
        const float newHeadingY = headingY + turnY;
        const float newHeadingLength = std::hypot(newHeadingX, newHeadingY);
        if (!(newHeadingLength > 1e-6f) || !std::isfinite(newHeadingLength)) {
            return;
        }
        traveller.directionX = newHeadingX / newHeadingLength * movementSign;
        traveller.directionY = newHeadingY / newHeadingLength * movementSign;
        traveller.velocityX = traveller.directionX * speed;
        traveller.velocityY = traveller.directionY * speed;
    };

    // Travellers move independently, so the simulation runs across all cores.
    std::vector<ParticleCanvas::Vertex> vertices(travellingPixels.size() * verticesPerTraveller);
    cv::parallel_for_(cv::Range(0, static_cast<int>(travellingPixels.size())),
                      [&](const cv::Range &range) {
        for (int index = range.start; index < range.end; ++index) {
            TravellingPixel &traveller = travellingPixels[index];
            if (steerBoids) {
                steer(traveller);
            }
            traveller.x += traveller.velocityX * pixelFlightStrength;
            traveller.y += traveller.velocityY * pixelFlightStrength;
            traveller.velocityX *= speedDecay;
            traveller.velocityY *= speedDecay;
            traveller.opacity *= fadeFactor;
            writeVertices(traveller, &vertices[index * verticesPerTraveller]);

            // Fully faded travellers can no longer change a pixel, so they end with the others.
            if (traveller.opacity * 255.0f < 1.0f ||
                std::hypot(traveller.velocityX, traveller.velocityY) < stopSpeed ||
                traveller.x < 0 || traveller.x >= width ||
                traveller.y < 0 || traveller.y >= height) {
                traveller.brightness = 0;
            }
        }
    });
    travellingPixels.erase(
        std::remove_if(travellingPixels.begin(), travellingPixels.end(),
                       [](const TravellingPixel &traveller) {
                           return traveller.brightness == 0;
                       }),
        travellingPixels.end());

    // A new source frame replaces the travellers waiting to be released.
    if (motionMeasured) {
        const std::size_t availableSlots = maxTravellers - travellingPixels.size();
        std::vector<TravellingPixel> &sampledTravellers = pendingTravellers;
        sampledTravellers.clear();
        sampledTravellers.reserve(std::min<std::size_t>(availableSlots, 65536));
        std::size_t eligiblePixels = 0;

        if (availableSlots > 0) {
            for (int y = 0; y < height; ++y) {
                const cv::Vec2f *flowRow = flow.ptr<cv::Vec2f>(y);
                const unsigned char *brightnessRow = inverted.ptr<unsigned char>(y);
                const unsigned char *movingRow = movingPixels.ptr<unsigned char>(y);
                const cv::Vec3b *colourRow = currentFrame.ptr<cv::Vec3b>(y);
                for (int x = 0; x < width; ++x) {
                    // Coloured particles move every moving pixel at its full flow speed.
                    if (movingRow[x] == 0 || (!colourMode && brightnessRow[x] == 0)) {
                        continue;
                    }

                    const cv::Vec2f movement = flowRow[x];
                    const float magnitude = std::hypot(movement[0], movement[1]);
                    if (magnitude < particleTuning::minFlowMagnitude) {
                        continue;
                    }

                    const float directionX = movement[0] / magnitude;
                    const float directionY = movement[1] / magnitude;
                    const float speed = colourMode ? magnitude
                                                   : magnitude * brightnessRow[x] / 255.0f;
                    if (speed < stopSpeed) {
                        continue;
                    }

                    const float originX = static_cast<float>(x) - movement[0];
                    const float originY = static_cast<float>(y) - movement[1];
                    const TravellingPixel traveller = {
                        originX,
                        originY,
                        directionX * speed,
                        directionY * speed,
                        directionX,
                        directionY,
                        speed,
                        colourMode ? static_cast<unsigned char>(255) : brightnessRow[x],
                        originX,
                        originY,
                        colourRow[x],
                        1.0f
                    };
                    if (sampledTravellers.size() < availableSlots) {
                        sampledTravellers.push_back(traveller);
                    } else {
                        const auto replacementIndex =
                            std::uniform_int_distribution<std::size_t>(0, eligiblePixels)(
                                travellerSampler);
                        if (replacementIndex < availableSlots) {
                            sampledTravellers[replacementIndex] = traveller;
                        }
                    }
                    ++eligiblePixels;
                }
            }
        }

    }

    // Each output frame releases every motionSpreadTicks-th waiting traveller, so new
    // particles arrive evenly instead of in one burst per source frame.
    if (injectMotion) {
        for (std::size_t index = static_cast<std::size_t>(motionSpreadTick);
             index < pendingTravellers.size() && travellingPixels.size() < maxTravellers;
             index += static_cast<std::size_t>(motionSpreadTicks)) {
            travellingPixels.push_back(pendingTravellers[index]);
            const std::size_t firstNewVertex = vertices.size();
            vertices.resize(firstNewVertex + verticesPerTraveller);
            writeVertices(pendingTravellers[index], &vertices[firstNewVertex]);
        }
    }

    // Moved travellers first, then new ones, in the same order the CPU used to draw them.
    particleCanvas.draw(target, vertices, drawPoints ? GL_POINTS : GL_LINES,
                        colourMode ? ParticleCanvas::Blend::Cover : ParticleCanvas::Blend::Add);
}

void ofApp::uploadSelectedInvertedLayer() {
    if (layerMode == LayerMode::DisplacementMap) {
        uploadProcessedPixels(displacementMapPixels);
    } else if (layerMode == LayerMode::DisplacedVideo) {
        uploadProcessedPixels(displacedVideoPixels);
    } else {
        uploadProcessedPixels(accumulatedInvertedFrame);
    }
}

void ofApp::uploadProcessedPixels(const ofPixels &pixels) {
    const int width = static_cast<int>(pixels.getWidth());
    const int height = static_cast<int>(pixels.getHeight());
    if (!processedTexture.isAllocated() || processedTexture.getWidth() != width ||
        processedTexture.getHeight() != height) {
        processedTexture.allocate(width, height, GL_RGB);
    }
    processedTexture.loadData(pixels);
    glFinish();
    hasProcessedFrame = true;
}

void ofApp::drawVideo() {
    ofBackground(0);
    ofSetColor(255);

    const float maxWidth = ofGetWidth();
    const float maxHeight = ofGetHeight();
    const float videoWidth = player.isLoaded() ? player.getWidth() : 0;
    const float videoHeight = player.isLoaded() ? player.getHeight() : 0;

    if (videoWidth > 0 && videoHeight > 0) {
        const float scale = std::min(maxWidth / videoWidth, maxHeight / videoHeight);
        const float drawWidth = videoWidth * scale;
        const float drawHeight = videoHeight * scale;
        const float x = (ofGetWidth() - drawWidth) * 0.5f;
        const float y = (maxHeight - drawHeight) * 0.5f;

        if (layerMode == LayerMode::OriginalVideo) {
            if (originalTexture.isAllocated()) originalTexture.draw(x, y, drawWidth, drawHeight);
        } else if (hasProcessedFrame) {
            processedTexture.draw(x, y, drawWidth, drawHeight);
        }
    }
    // Finish reads before the processing context can replace the shared image.
    glFinish();
}

void ofApp::draw() {
    ofBackground(12, 14, 18);
    ofSetColor(235);
    saveConfigButton.set(28, 55, 140, 27);
    ofDrawRectangle(saveConfigButton);
    ofSetColor(12, 14, 18);
    ofDrawBitmapString("Save config", 48, 74);
    ofSetColor(235);
    ofDrawBitmapString("settings.toml: " + settingsStatus, 28, 104);
    hysteresisToggle.set(28, 155, 275, 27);
    ofSetColor(hysteresisThresholding ? ofColor(72, 95, 115) : ofColor(38, 42, 48));
    ofDrawRectangle(hysteresisToggle);
    ofSetColor(235);
    ofDrawBitmapString(std::string(hysteresisThresholding ? "[x]" : "[ ]") +
                           " Hysteresis thresholding", 35, 173);
    polygonGateToggle.set(318, 155, 275, 27);
    ofSetColor(polygonGate ? ofColor(72, 95, 115) : ofColor(38, 42, 48));
    ofDrawRectangle(polygonGateToggle);
    ofSetColor(235);
    ofDrawBitmapString(std::string(polygonGate ? "[x]" : "[ ]") + " Polygon gate", 325, 173);
    ofDrawBitmapString("Polygon detail", 610, 173);
    polygonDetailField.set(735, 155, 95, 27);
    const bool editingDetail = editingFloatInput == FloatInput::PolygonDetail;
    ofSetColor(editingDetail ? ofColor(72, 95, 115) : ofColor(38, 42, 48));
    ofDrawRectangle(polygonDetailField);
    ofNoFill();
    ofSetColor(115, 125, 138);
    ofDrawRectangle(polygonDetailField);
    ofFill();
    ofSetColor(235);
    ofDrawBitmapString(editingDetail ? floatInputText : ofToString(polygonDetail, 2),
                       polygonDetailField.x + 7, polygonDetailField.y + 18);
    ofSetColor(155);
    ofDrawBitmapString("0-100", polygonDetailField.getRight() + 10, 173);
    objectCutoutToggle.set(890, 155, 180, 27);
    ofSetColor(objectCutout ? ofColor(72, 95, 115) : ofColor(38, 42, 48));
    ofDrawRectangle(objectCutoutToggle);
    ofSetColor(235);
    ofDrawBitmapString(std::string(objectCutout ? "[x]" : "[ ]") + " Object cutout", 897, 173);
    ofSetColor(235);

    if (sources.empty()) {
        ofDrawBitmapString(renderStatus, 28, 135);
        return;
    }

    ofSetColor(235);
    const std::string layerName = layerModeName();
    const std::string videoName = std::filesystem::path(sources[selectedVideo].path).filename().string();
    const std::string fpsLabel = useSixtyFps
                                     ? "60 FPS"
                                     : "Source FPS " + ofToString(sources[selectedVideo].averageFps, 2);
    std::string traceCount = "Traces: " + ofToString(travellingPixels.size()) +
                             " / " + ofToString(maxTravellers);
    if (travellingPixels.size() >= maxTravellers) {
        traceCount += " FULL";
    }
    ofDrawBitmapString("" + ofToString(selectedVideo + 1) + "  " + videoName +
                           "   |   " + layerName + "   |   Offset: " +
                           ofToString(frameOffset) + " frames   |   " + fpsLabel +
                           "   |   Saved specs: " +
                           ofToString(savedSpecCount(selectedVideo)),
                       28, ofGetHeight() - 185);
    ofDrawBitmapString(traceCount, ofGetWidth() - 320, ofGetHeight() - 158);
    ofSetColor(155);
    drawPixelControls();
    ofDrawBitmapString("[ / ]: previous / next video    Left / Right: layer    Up / Down: offset    F: source / 60 FPS",
                       28, ofGetHeight() - 28);
    if (!renderStatus.empty()) {
        ofSetColor(235);
        ofDrawBitmapString(renderStatus, 28, 24);
    }
    // Drawn last so the open list covers the video and other controls.
    drawTreatmentMenu();
}

void ofApp::keyPressed(int key) {
    processingWindow->makeCurrent();
    if (editingFloatInput != FloatInput::None) {
        if (key == OF_KEY_RETURN) {
            if (!floatInputText.empty()) {
                char *end = nullptr;
                const float enteredValue = std::strtof(floatInputText.c_str(), &end);
                if (end != floatInputText.c_str() && *end == '\0' &&
                    std::isfinite(enteredValue)) {
                    if (editingFloatInput == FloatInput::PixelFade) {
                        pixelFadePercent = enteredValue;
                    } else if (editingFloatInput == FloatInput::FlightStrength) {
                        pixelFlightStrength = enteredValue;
                    } else if (editingFloatInput == FloatInput::PolygonDetail) {
                        polygonDetail = std::clamp(enteredValue, 0.0f, 100.0f);
                    } else if (editingFloatInput == FloatInput::SpeedContrast) {
                        speedContrast = std::clamp(enteredValue, 0.0f, 100.0f);
                    } else {
                        trailSpeedFactor = enteredValue;
                    }
                }
            }
            editingFloatInput = FloatInput::None;
            floatInputText.clear();
            return;
        }
        if (key == OF_KEY_ESC) {
            editingFloatInput = FloatInput::None;
            floatInputText.clear();
            return;
        }
        if (key == OF_KEY_BACKSPACE || key == OF_KEY_DEL) {
            if (!floatInputText.empty()) {
                floatInputText.pop_back();
            }
            return;
        }
        if (key >= '0' && key <= '9') {
            floatInputText.push_back(static_cast<char>(key));
            return;
        }
        if ((key == '-' || key == '+') && floatInputText.empty()) {
            floatInputText.push_back(static_cast<char>(key));
            return;
        }
        if (key == '.' && floatInputText.find('.') == std::string::npos) {
            floatInputText.push_back(static_cast<char>(key));
            return;
        }
        return;
    }

    if (sources.empty()) {
        return;
    }
    if (key == '[' || key == ']') {
        loadVideo(key == ']' ? (selectedVideo + 1) % sources.size()
                             : (selectedVideo + sources.size() - 1) % sources.size());
        return;
    }

    if (key == OF_KEY_LEFT || key == OF_KEY_RIGHT) {
        const int layerCount = 6;
        const int currentLayer = static_cast<int>(layerMode);
        const int direction = key == OF_KEY_RIGHT ? 1 : -1;
        const int nextLayer = (currentLayer + direction + layerCount) % layerCount;
        sessionSettings[layerModeName()] = currentSettings();
        layerMode = static_cast<LayerMode>(nextLayer);
        const auto mode = layerModeName();
        const auto saved = sessionSettings.find(mode);
        if (!interactiveConfig.global.empty()) {
            applySettings(saved == sessionSettings.end() ? interactiveConfig.resolve(mode) : saved->second);
        }
        hasProcessedFrame = false;
        if (layerMode == LayerMode::InvertedGrayscale ||
            layerMode == LayerMode::DisplacementMap ||
            layerMode == LayerMode::DisplacedVideo) {
            invertedLayerInitialized = false;
            travellingPixels.clear();
        }
        return;
    }
    if (key == 'f') {
        useSixtyFps = !useSixtyFps;
        applyTargetFrameRate();
        hasProcessedFrame = false;
        return;
    }

    if (key == OF_KEY_UP && frameOffset < 120) {
        ++frameOffset;
        sourceFrameHistory.clear();
        hasProcessedFrame = false;
    } else if (key == OF_KEY_DOWN && frameOffset > 0) {
        --frameOffset;
        sourceFrameHistory.clear();
        hasProcessedFrame = false;
    }
}

void ofApp::drawPixelControls() {
    const float y = ofGetHeight() - 135;
    fadeValueField.set(205, y + 2, 95, 27);
    flightStrengthField.set(440, y + 2, 95, 27);
    trailSpeedFactorField.set(700, y + 2, 105, 27);
    speedContrastField.set(205, y - 33, 95, 27);
    saveSpecsButton.set(850, y + 2, 125, 27);
    treatmentField.set(205, y + 37, 125, 27);
    particlesToggle.set(345, y + 37, 230, 27);
    particlesOnlyToggle.set(590, y + 37, 170, 27);
    boidsToggle.set(775, y + 37, 95, 27);

    ofSetColor(235);
    ofDrawBitmapString("Pixel fade (% / frame)", 28, y + 20);
    ofDrawBitmapString("Speed contrast", 28, y - 15);
    ofDrawBitmapString("Flight strength", 320, y + 20);
    ofDrawBitmapString("Trail speed factor", 540, y + 20);
    ofDrawBitmapString("Pixel treatment", 28, y + 55);

    const auto drawField = [](const ofRectangle &rect, const std::string &value,
                              bool selected) {
        ofSetColor(selected ? ofColor(72, 95, 115) : ofColor(38, 42, 48));
        ofDrawRectangle(rect);
        ofNoFill();
        ofSetColor(115, 125, 138);
        ofDrawRectangle(rect);
        ofFill();
        ofSetColor(235);
        ofDrawBitmapString(value, rect.x + 7, rect.y + 18);
    };

    const std::string fadeValue = editingFloatInput == FloatInput::PixelFade
                                      ? floatInputText
                                      : ofToString(pixelFadePercent, 2);
    const std::string flightValue = editingFloatInput == FloatInput::FlightStrength
                                        ? floatInputText
                                        : ofToString(pixelFlightStrength, 2);
    const std::string trailSpeedFactorValue =
        editingFloatInput == FloatInput::TrailSpeedFactor
            ? floatInputText
            : ofToString(trailSpeedFactor, 2);
    drawField(fadeValueField, fadeValue, editingFloatInput == FloatInput::PixelFade);
    drawField(flightStrengthField, flightValue,
              editingFloatInput == FloatInput::FlightStrength);
    drawField(trailSpeedFactorField, trailSpeedFactorValue,
              editingFloatInput == FloatInput::TrailSpeedFactor);
    drawField(speedContrastField,
              editingFloatInput == FloatInput::SpeedContrast ? floatInputText
                                                             : ofToString(speedContrast, 2),
              editingFloatInput == FloatInput::SpeedContrast);
    ofSetColor(155);
    ofDrawBitmapString("0-100; coloured particles gain contrast with speed (0 = off)",
                       speedContrastField.getRight() + 15, speedContrastField.y + 18);
    drawField(treatmentField, pixelTreatmentName(pixelTreatment), treatmentMenuOpen);
    ofSetColor(235);
    ofDrawTriangle(treatmentField.getRight() - 18, treatmentField.y + 11,
                   treatmentField.getRight() - 8, treatmentField.y + 11,
                   treatmentField.getRight() - 13, treatmentField.y + 17);
    drawField(particlesToggle,
              std::string(colouredParticles ? "[x]" : "[ ]") + " Coloured particles",
              colouredParticles);
    drawField(particlesOnlyToggle,
              std::string(particlesOnly ? "[x]" : "[ ]") + " Particles only", particlesOnly);
    drawField(boidsToggle, std::string(boids ? "[x]" : "[ ]") + " Boids", boids);
    ofSetColor(155);
    ofDrawBitmapString(colouredParticles ? "Layers push pixels" : "Layers pull via field",
                       boidsToggle.getRight() + 15, boidsToggle.y + 18);
    ofSetColor(48, 87, 67);
    ofDrawRectangle(saveSpecsButton);
    ofNoFill();
    ofSetColor(115, 165, 130);
    ofDrawRectangle(saveSpecsButton);
    ofFill();
    ofSetColor(245);
    ofDrawBitmapString("Save specs", saveSpecsButton.x + 24, saveSpecsButton.y + 18);
    ofSetColor(155);
    ofDrawBitmapString("Positive trail speed factor shortens trails more at higher speeds. Click a value, type a float, then Enter. Esc cancels.",
                       28, ofGetHeight() - 50);
}

ofRectangle ofApp::treatmentOptionRect(std::size_t index) const {
    // The list opens upward because the field sits near the bottom of the window.
    constexpr std::size_t optionCount = 3;
    return ofRectangle(treatmentField.x,
                       treatmentField.y - (optionCount - index) * treatmentField.height,
                       treatmentField.width, treatmentField.height);
}

void ofApp::drawTreatmentMenu() {
    if (!treatmentMenuOpen) {
        return;
    }
    const PixelTreatment options[] = {PixelTreatment::Smear, PixelTreatment::Streak,
                                      PixelTreatment::Stretch};
    for (std::size_t index = 0; index < 3; ++index) {
        const ofRectangle rect = treatmentOptionRect(index);
        const bool hovered = rect.inside(ofGetMouseX(), ofGetMouseY());
        ofSetColor(options[index] == pixelTreatment ? ofColor(72, 95, 115)
                   : hovered                         ? ofColor(55, 62, 72)
                                                     : ofColor(38, 42, 48));
        ofDrawRectangle(rect);
        ofNoFill();
        ofSetColor(115, 125, 138);
        ofDrawRectangle(rect);
        ofFill();
        ofSetColor(235);
        ofDrawBitmapString(pixelTreatmentName(options[index]), rect.x + 7, rect.y + 18);
    }
}

void ofApp::mousePressed(int x, int y, int button) {
    processingWindow->makeCurrent();
    if (button != OF_MOUSE_BUTTON_LEFT) {
        return;
    }
    if (saveConfigButton.inside(x, y)) {
        saveSettings();
        return;
    }
    if (hysteresisToggle.inside(x, y)) {
        auto settings = currentSettings();
        settings.hysteresisThresholding = !settings.hysteresisThresholding;
        applySettings(settings);
        return;
    }
    if (polygonDetailField.inside(x, y)) {
        editingFloatInput = FloatInput::PolygonDetail;
        floatInputText.clear();
        return;
    }
    if (objectCutoutToggle.inside(x, y)) {
        auto settings = currentSettings();
        settings.objectCutout = !settings.objectCutout;
        applySettings(settings);
        return;
    }
    if (polygonGateToggle.inside(x, y)) {
        auto settings = currentSettings();
        settings.polygonGate = !settings.polygonGate;
        applySettings(settings);
        return;
    }
    if (sources.empty()) return;
    if (treatmentMenuOpen) {
        const PixelTreatment options[] = {PixelTreatment::Smear, PixelTreatment::Streak,
                                          PixelTreatment::Stretch};
        for (std::size_t index = 0; index < 3; ++index) {
            if (treatmentOptionRect(index).inside(x, y)) {
                pixelTreatment = options[index];
            }
        }
        treatmentMenuOpen = false;
        return;
    }
    if (treatmentField.inside(x, y)) {
        editingFloatInput = FloatInput::None;
        floatInputText.clear();
        treatmentMenuOpen = true;
        return;
    }
    if (particlesToggle.inside(x, y)) {
        editingFloatInput = FloatInput::None;
        floatInputText.clear();
        colouredParticles = !colouredParticles;
        invertedLayerInitialized = false;
        travellingPixels.clear();
        return;
    }
    if (particlesOnlyToggle.inside(x, y)) {
        editingFloatInput = FloatInput::None;
        floatInputText.clear();
        particlesOnly = !particlesOnly;
        // Drop old smear paint so the switch shows at once.
        if (particlesOnly && displacedVideoPixels.isAllocated()) {
            std::memset(displacedVideoPixels.getData(), 0, displacedVideoPixels.size());
        }
        return;
    }
    if (boidsToggle.inside(x, y)) {
        editingFloatInput = FloatInput::None;
        floatInputText.clear();
        boids = !boids;
        return;
    }
    if (saveSpecsButton.inside(x, y)) {
        editingFloatInput = FloatInput::None;
        floatInputText.clear();
        saveRenderSpecs();
    } else if (fadeValueField.inside(x, y)) {
        editingFloatInput = FloatInput::PixelFade;
    } else if (flightStrengthField.inside(x, y)) {
        editingFloatInput = FloatInput::FlightStrength;
    } else if (trailSpeedFactorField.inside(x, y)) {
        editingFloatInput = FloatInput::TrailSpeedFactor;
    } else if (speedContrastField.inside(x, y)) {
        editingFloatInput = FloatInput::SpeedContrast;
    } else {
        editingFloatInput = FloatInput::None;
    }
    floatInputText.clear();
}

bool ofApp::renderJob(std::size_t videoIndex, std::size_t specIndex,
                      const std::filesystem::path &outputDirectory, std::size_t maxFrames,
                      RenderProgress &progress) {
    const RenderSpec &spec = renderSpecs[videoIndex][specIndex];
    const std::string label = sources[videoIndex].label + " spec " +
                              ofToString(static_cast<int>(specIndex + 1), 2, '0');
    auto fail = [&](const std::string &message) {
        ofLogError("render") << label << ": " << message;
        progress.error = message;
        return false;
    };
    selectedVideo = videoIndex;
    frameOffset = spec.frameOffset;
    pixelFadePercent = spec.fadePercent;
    pixelFlightStrength = spec.flightStrength;
    trailSpeedFactor = spec.trailSpeedFactor;
    layerMode = layerModeFromName(spec.mode);
    pixelTreatment = pixelTreatmentFromName(spec.treatment);
    colouredParticles = spec.colouredParticles;
    particlesOnly = spec.particlesOnly;
    boids = spec.boids;
    boidTuning = spec.boidTuning;
    hysteresisThresholding = spec.hysteresisThresholding;
    polygonGate = spec.polygonGate;
    polygonDetail = spec.polygonDetail;
    objectCutout = spec.objectCutout;
    speedContrast = spec.speedContrast;
    resetForeground();
    motionMaskPixels.clear();
    sourceFrameHistory.clear();
    accumulatedInvertedFrame.clear();
    displacementMapPixels.clear();
    displacedVideoPixels.clear();
    displacementField.release();
    travellingPixels.clear();
    travellerSampler.seed(0x6f70656e4657ULL);
    invertedLayerInitialized = false;

    const auto inputPath = appDataPath(sources[videoIndex].path);
    cv::VideoCapture capture;
    if (!capture.open(inputPath.string())) {
        return fail("could not open " + inputPath.string());
    }

    const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    const auto &videoDirectory = outputDirectory;
    const std::string videoName = ofToLower(sources[videoIndex].label) + "-spec-" +
                                  ofToString(static_cast<int>(specIndex + 1), 2, '0');
    const auto videoPath = videoDirectory / (videoName + ".mp4");
    const auto partialPath = videoDirectory / (videoName + ".partial.mp4");
    std::filesystem::create_directories(videoDirectory);
    if (partialPath.string().find('\'') != std::string::npos) {
        return fail("output path contains a quote: " + partialPath.string());
    }

    // Frames go to ffmpeg as raw RGB; the encode matches the earlier renders (x264, CRF 18).
    const std::string command =
        "ffmpeg -v error -y -f rawvideo -pix_fmt rgb24 -s " + ofToString(width) + "x" +
        ofToString(height) + " -r " + ofToString(spec.fps) + " -i - " +
        "-vf 'pad=ceil(iw/2)*2:ceil(ih/2)*2' -c:v libx264 -crf 18 -pix_fmt yuv420p " +
        "-movflags +faststart '" + partialPath.string() + "'";
    FILE *encoder = popen(command.c_str(), "w");
    if (encoder == nullptr) {
        return fail("could not start ffmpeg");
    }
    auto finishEncoder = [&](bool framesOk) {
        const int status = pclose(encoder);
        if (framesOk && status == 0) {
            std::filesystem::rename(partialPath, videoPath);
            return true;
        }
        std::filesystem::remove(partialPath);
        return fail("ffmpeg failed (status " + ofToString(status) + ")");
    };

    ofLogNotice("render") << label << ": " << spec.mode << " at " << spec.fps
                          << " fps, offset " << frameOffset << " -> " << videoPath;
    const auto startTime = std::chrono::steady_clock::now();
    const float sourceFps = sources[videoIndex].averageFps;
    motionSpreadTicks = std::max(1, static_cast<int>(std::lround(spec.fps / sourceFps)));
    double tickRemainder = 0;
    std::size_t frameCount = 0;
    ofPixels sourceFrame;
    cv::Mat bgrFrame;
    cv::Mat rgbFrame;
    while ((maxFrames == 0 || frameCount < maxFrames) && capture.read(bgrFrame)) {
        cv::cvtColor(bgrFrame, rgbFrame, cv::COLOR_BGR2RGB);
        sourceFrame.setFromPixels(rgbFrame.data, rgbFrame.cols, rgbFrame.rows, OF_PIXELS_RGB);
        // Each source frame becomes zero or more output frames at the spec frame rate.
        tickRemainder += spec.fps / sourceFps;
        const int ticks = static_cast<int>(std::floor(tickRemainder));
        tickRemainder -= ticks;

        for (int tick = 0; tick < ticks && (maxFrames == 0 || frameCount < maxFrames); ++tick) {
            if (tick == 0) {
                pushSourceFrame(sourceFrame);
            }
            // Same layer routing as update(): only the layers that use it pay for optical flow.
            if (layerMode == LayerMode::MotionMask || layerMode == LayerMode::MotionPolygons) {
                updateMotionFrame();
            } else if (layerMode != LayerMode::OriginalVideo) {
                updateInvertedFrame(sourceFrame, tick == 0);
            }

            const ofPixels *output = &accumulatedInvertedFrame;
            if (layerMode == LayerMode::MotionMask || layerMode == LayerMode::MotionPolygons) {
                output = &motionMaskPixels;
            } else if (layerMode == LayerMode::DisplacementMap) {
                output = &displacementMapPixels;
            } else if (layerMode == LayerMode::DisplacedVideo) {
                output = &displacedVideoPixels;
            } else if (layerMode == LayerMode::OriginalVideo) {
                output = &sourceFrame;
            }
            if (std::fwrite(output->getData(), 1, output->size(), encoder) != output->size()) {
                finishEncoder(false);
                return false;
            }

            ++frameCount;
            progress.frames = frameCount;
            if (frameCount % 250 == 0) {
                const double seconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - startTime).count();
                ofLogNotice("render") << label << ": " << frameCount << " frames, "
                                      << ofToString(frameCount / seconds, 2) << " fps";
            }
        }
    }
    if (!finishEncoder(true)) {
        return false;
    }

    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startTime).count();
    ofLogNotice("render") << label << ": finished " << frameCount << " frames in "
                          << ofToString(seconds, 1) << " s ("
                          << ofToString(frameCount / std::max(seconds, 0.001), 2) << " fps)";
    return true;
}

int ofApp::runRenderCommand(const std::vector<std::string> &args) {
    // A failed ffmpeg must surface as a write error, not terminate the process.
    std::signal(SIGPIPE, SIG_IGN);
    const unsigned int cores = std::max(1u, std::thread::hardware_concurrency());
    std::size_t jobCount = std::max(1u, cores / 3);
    std::size_t maxFrames = 0;
    std::filesystem::path specsPath = appDataPath("render-specs.toml");
    // Each run gets its own folder, so earlier videos are never overwritten.
    std::filesystem::path outputDirectory =
        appDataPath("renders/inverted-motion-trails/video/" +
                    ofGetTimestampString("%Y%m%d-%H%M%S"));
    std::string only;

    auto usage = []() {
        ofLogError("render")
            << "usage: helloOpenCv --render [--jobs N] [--frames N] [--only video[/spec]]"
               " [--specs file.toml] [--out directory]";
        return 2;
    };
    auto parseCount = [](const std::string &text, std::size_t &value) {
        char *end = nullptr;
        const long parsed = std::strtol(text.c_str(), &end, 10);
        if (text.empty() || *end != '\0' || parsed < 0) {
            return false;
        }
        value = static_cast<std::size_t>(parsed);
        return true;
    };
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string &option = args[index];
        if (index + 1 >= args.size()) {
            return usage();
        }
        const std::string &value = args[++index];
        if (option == "--jobs") {
            if (!parseCount(value, jobCount) || jobCount == 0) {
                return usage();
            }
        } else if (option == "--frames") {
            if (!parseCount(value, maxFrames)) {
                return usage();
            }
        } else if (option == "--only") {
            only = ofToLower(value);
        } else if (option == "--specs") {
            specsPath = std::filesystem::absolute(value);
        } else if (option == "--out") {
            outputDirectory = std::filesystem::absolute(value);
        } else {
            return usage();
        }
    }

    ofApp queue;
    queue.discoverVideos();
    if (!queue.loadRenderSpecs(specsPath)) return 1;
    std::vector<std::pair<std::size_t, std::size_t>> jobs;
    for (std::size_t video = 0; video < queue.renderSpecs.size(); ++video) {
        for (std::size_t spec = 0; spec < queue.renderSpecs[video].size(); ++spec) {
            const std::string name = ofToLower(queue.sources[video].label);
            if (only.empty() || only == name || only == name + "/" + ofToString(spec + 1)) {
                jobs.emplace_back(video, spec);
            }
        }
    }
    if (jobs.empty()) {
        ofLogError("render") << "no queued specs match" << (only.empty() ? "" : " " + only)
                             << " in " << specsPath;
        return 1;
    }

    // Keep the queue next to the videos so the run can be reproduced.
    std::filesystem::create_directories(outputDirectory);
    std::filesystem::copy_file(specsPath, outputDirectory / "render-specs.toml",
                               std::filesystem::copy_options::overwrite_existing);

    std::vector<RenderProgress> progress(jobs.size());
    for (std::size_t job = 0; job < jobs.size(); ++job) {
        const auto [video, spec] = jobs[job];
        const RenderSpec &renderSpec = queue.renderSpecs[video][spec];
        progress[job].name = ofToLower(queue.sources[video].label) + "/" + ofToString(spec + 1);
        progress[job].mode = renderSpec.mode;
        // Same frame-rate conversion as renderJob, from the container's frame count.
        cv::VideoCapture capture(appDataPath(queue.sources[video].path).string());
        const double sourceFrames = std::max(0.0, capture.get(cv::CAP_PROP_FRAME_COUNT));
        progress[job].expectedFrames = static_cast<std::size_t>(
            sourceFrames * renderSpec.fps / queue.sources[video].averageFps);
        if (maxFrames > 0) {
            progress[job].expectedFrames = std::min(progress[job].expectedFrames, maxFrames);
        }
    }

    jobCount = std::min(jobCount, jobs.size());
    const bool liveStatus = isatty(STDOUT_FILENO) != 0;
    std::cout << jobs.size() << (jobs.size() == 1 ? " spec, " : " specs, ") << jobCount
              << " at once -> "
              << outputDirectory.string() << std::endl;
    if (liveStatus) {
        // The status table replaces log lines; errors are listed after it.
        ofSetLogLevel(OF_LOG_SILENT);
    }

    // Particles are drawn with OpenGL, so each worker gets its own offscreen GL 2.1 context.
    std::vector<CGLContextObj> contexts;
    auto destroyContexts = [&]() {
        CGLSetCurrentContext(nullptr);
        for (CGLContextObj context : contexts) {
            CGLDestroyContext(context);
        }
        contexts.clear();
    };
    {
        const CGLPixelFormatAttribute attributes[] = {
            kCGLPFAAccelerated,
            kCGLPFAOpenGLProfile, static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_Legacy),
            static_cast<CGLPixelFormatAttribute>(0)};
        CGLPixelFormatObj pixelFormat = nullptr;
        GLint formatCount = 0;
        if (CGLChoosePixelFormat(attributes, &pixelFormat, &formatCount) != kCGLNoError ||
            pixelFormat == nullptr) {
            std::cerr << "render: no accelerated OpenGL pixel format" << std::endl;
            return 1;
        }
        for (std::size_t worker = 0; worker < jobCount; ++worker) {
            CGLContextObj context = nullptr;
            if (CGLCreateContext(pixelFormat, nullptr, &context) != kCGLNoError) {
                CGLDestroyPixelFormat(pixelFormat);
                destroyContexts();
                std::cerr << "render: could not create an OpenGL context" << std::endl;
                return 1;
            }
            contexts.push_back(context);
        }
        CGLDestroyPixelFormat(pixelFormat);
        // GLEW loads the GL entry points once; they are shared by every context.
        CGLSetCurrentContext(contexts.front());
        glewExperimental = GL_TRUE;
        const GLenum glewStatus = glewInit();
        CGLSetCurrentContext(nullptr);
        if (glewStatus != GLEW_OK) {
            destroyContexts();
            std::cerr << "render: could not load OpenGL functions" << std::endl;
            return 1;
        }
    }

    const auto startTime = std::chrono::steady_clock::now();
    auto secondsSinceStart = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime)
            .count();
    };
    std::atomic<std::size_t> nextJob{0};
    std::atomic<std::size_t> finishedJobs{0};
    std::vector<std::thread> workers;
    for (std::size_t worker = 0; worker < jobCount; ++worker) {
        workers.emplace_back([&, worker]() {
            CGLSetCurrentContext(contexts[worker]);
            {
                // Each worker owns a separate processing state, destroyed while its
                // context is still current so its GL objects are released.
                ofApp renderer;
                renderer.sources = queue.sources;
                renderer.renderSpecs = queue.renderSpecs;
                for (std::size_t job = nextJob++; job < jobs.size(); job = nextJob++) {
                    RenderProgress &jobProgress = progress[job];
                    jobProgress.startSeconds = secondsSinceStart();
                    jobProgress.state = RenderState::Running;
                    const bool rendered = renderer.renderJob(jobs[job].first, jobs[job].second,
                                                             outputDirectory, maxFrames,
                                                             jobProgress);
                    jobProgress.endSeconds = secondsSinceStart();
                    jobProgress.state = rendered ? RenderState::Done : RenderState::Failed;
                    ++finishedJobs;
                }
            }
            CGLSetCurrentContext(nullptr);
        });
    }

    auto clock = [](double seconds) {
        const int total = static_cast<int>(std::max(0.0, seconds));
        const int hours = total / 3600;
        const std::string minutesSeconds = ofToString((total / 60) % 60, 2, '0') + ":" +
                                           ofToString(total % 60, 2, '0');
        return hours > 0 ? ofToString(hours) + ":" + minutesSeconds : minutesSeconds;
    };
    auto bar = [](double fraction, int width) {
        const int filled = static_cast<int>(std::clamp(fraction, 0.0, 1.0) * width);
        std::string text;
        for (int cell = 0; cell < width; ++cell) {
            text += cell < filled ? "█" : "░";
        }
        return text;
    };
    std::size_t nameWidth = 0;
    std::size_t modeWidth = 0;
    for (const RenderProgress &jobProgress : progress) {
        nameWidth = std::max(nameWidth, jobProgress.name.size());
        modeWidth = std::max(modeWidth, jobProgress.mode.size());
    }
    auto pad = [](const std::string &text, std::size_t width) {
        return text + std::string(width > text.size() ? width - text.size() : 0, ' ');
    };
    auto rate = [](std::size_t frames, double seconds) {
        return seconds > 0.5 ? frames / seconds : 0.0;
    };
    constexpr int barWidth = 24;
    std::size_t drawnLines = 0;
    auto drawStatus = [&]() {
        const double now = secondsSinceStart();
        std::vector<std::string> lines;
        std::size_t doneFrames = 0;
        std::size_t expectedFrames = 0;
        std::size_t done = 0;
        std::size_t failed = 0;
        std::size_t running = 0;
        for (const RenderProgress &jobProgress : progress) {
            const RenderState state = jobProgress.state;
            const std::size_t frames = jobProgress.frames;
            const double started = jobProgress.startSeconds;
            doneFrames += frames;
            expectedFrames += state == RenderState::Done
                                  ? frames
                                  : std::max(frames, jobProgress.expectedFrames);
            std::string line = "  " + pad(jobProgress.name, nameWidth) + "  " +
                               pad(jobProgress.mode, modeWidth) + "  ";
            if (state == RenderState::Queued) {
                line = "\033[2m" + line + "queued\033[0m";
            } else if (state == RenderState::Running) {
                ++running;
                const double fps = rate(frames, now - started);
                const double fraction = jobProgress.expectedFrames > 0
                                            ? double(frames) / jobProgress.expectedFrames
                                            : 0.0;
                const std::size_t remaining =
                    jobProgress.expectedFrames > frames ? jobProgress.expectedFrames - frames : 0;
                line = "\033[33m" + line + bar(fraction, barWidth) + "\033[0m  " +
                       ofToString(frames) + "/" + ofToString(jobProgress.expectedFrames) + "  " +
                       ofToString(static_cast<int>(fraction * 100)) + "%  " +
                       ofToString(fps, 1) + " fps  eta " +
                       (fps > 0 ? clock(remaining / fps) : std::string("--:--"));
            } else if (state == RenderState::Done) {
                ++done;
                const double seconds = jobProgress.endSeconds - started;
                line = "\033[32m" + line + "done\033[0m  " + ofToString(frames) + " frames in " +
                       clock(seconds) + "  " + ofToString(rate(frames, seconds), 1) + " fps";
            } else {
                ++failed;
                line = "\033[31m" + line + "failed: " + jobProgress.error + "\033[0m";
            }
            lines.push_back(line);
        }
        const double totalFps = rate(doneFrames, now);
        const double fraction = expectedFrames > 0 ? double(doneFrames) / expectedFrames : 0.0;
        const std::size_t remaining = expectedFrames > doneFrames ? expectedFrames - doneFrames : 0;
        lines.insert(lines.begin(),
                     "\033[1mqueue\033[0m  " + ofToString(done) + "/" +
                         ofToString(progress.size()) + " done  " + ofToString(running) +
                         " running  " + ofToString(failed) + " failed  " + clock(now) +
                         " elapsed");
        lines.insert(lines.begin() + 1,
                     "  " + pad("total", nameWidth + modeWidth + 2) + "  " +
                         bar(fraction, barWidth) + "  " + ofToString(doneFrames) + "/" +
                         ofToString(expectedFrames) + "  " +
                         ofToString(static_cast<int>(fraction * 100)) + "%  " +
                         ofToString(totalFps, 1) + " fps  eta " +
                         (totalFps > 0 && running > 0 ? clock(remaining / totalFps)
                                                      : std::string("--:--")));

        // Move back over the previous table and redraw it in place.
        std::string frame;
        if (drawnLines > 0) {
            frame += "\033[" + ofToString(drawnLines) + "F";
        }
        for (const std::string &line : lines) {
            frame += "\033[2K" + line + "\n";
        }
        std::cout << frame << std::flush;
        drawnLines = lines.size();
    };

    if (liveStatus) {
        while (finishedJobs < jobs.size()) {
            drawStatus();
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    for (auto &worker : workers) {
        worker.join();
    }
    destroyContexts();
    std::size_t failures = 0;
    for (const RenderProgress &jobProgress : progress) {
        failures += jobProgress.state == RenderState::Failed ? 1 : 0;
    }
    if (liveStatus) {
        drawStatus();
        ofSetLogLevel(OF_LOG_NOTICE);
        for (const RenderProgress &jobProgress : progress) {
            if (jobProgress.state == RenderState::Failed) {
                std::cout << jobProgress.name << ": " << jobProgress.error << "\n";
            }
        }
    }

    std::cout << "done: " << (jobs.size() - failures) << "/" << jobs.size() << " specs in "
              << clock(secondsSinceStart()) << " -> " << outputDirectory.string() << std::endl;
    return failures == 0 ? 0 : 1;
}
