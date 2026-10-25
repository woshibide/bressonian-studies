#include "ofApp.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace {

constexpr std::size_t maxTravellingPixels = 2500000;

of::filesystem::path appDataPath(const of::filesystem::path &relativePath) {
    return (ofFilePath::getCurrentExeDirFS() / "../../../data" / relativePath)
        .lexically_normal();
}

} // namespace

void ofApp::setup() {
    ofSetWindowTitle("CCTV Motion Difference");
    ofSetWindowShape(1100, 720);
    ofSetVerticalSync(false);

    loadRenderSpecs(appDataPath("render-specs.json"));

    loadVideo(0);
}

void ofApp::loadRenderSpecs(const std::filesystem::path &specsPath) {
    renderSpecs.assign(sources.size(), {});
    selectedSpecIndices.assign(sources.size(), 0);

    if (!std::filesystem::exists(specsPath)) {
        return;
    }

    const ofJson document = ofLoadJson(specsPath);
    if (!document.is_object() ||
        !document.contains("videos") || !document["videos"].is_array()) {
        ofLogError("ofApp") << "Could not read render specs: " << specsPath;
        return;
    }

    for (const auto &entry : document["videos"]) {
        if (!entry.is_object() || !entry.contains("source") ||
            !entry["source"].is_string()) {
            continue;
        }
        const std::string source = entry["source"].get<std::string>();
        const auto found = std::find_if(sources.begin(), sources.end(),
                                        [&](const VideoSource &video) {
                                            return video.label == source;
                                        });
        if (found == sources.end()) {
            continue;
        }
        const auto index = static_cast<std::size_t>(std::distance(sources.begin(), found));
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
}

std::size_t ofApp::savedSpecCount(std::size_t videoIndex) const {
    return videoIndex < renderSpecs.size() ? renderSpecs[videoIndex].size() : 0;
}

ofApp::LayerMode ofApp::layerModeFromName(const std::string &name) const {
    if (name == "motionMask") {
        return LayerMode::MotionMask;
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

std::string ofApp::layerModeName() const {
    switch (layerMode) {
    case LayerMode::MotionMask:
        return "motionMask";
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

    // Preserve queue edits made directly to the JSON since the app was opened.
    loadRenderSpecs(appDataPath("render-specs.json"));

    RenderSpec spec;
    spec.frameOffset = frameOffset;
    spec.fadePercent = pixelFadePercent;
    spec.flightStrength = pixelFlightStrength;
    spec.trailSpeedFactor = trailSpeedFactor;
    spec.fps = 60.0f;
    spec.mode = layerModeName();
    renderSpecs[selectedVideo].push_back(spec);
    selectedSpecIndices[selectedVideo] = renderSpecs[selectedVideo].size() - 1;

    ofJson document;
    document["version"] = 1;
    document["videos"] = ofJson::array();
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (renderSpecs[index].empty()) {
            continue;
        }
        ofJson savedSpecs = ofJson::array();
        for (std::size_t specIndex = 0; specIndex < renderSpecs[index].size(); ++specIndex) {
            const RenderSpec &entry = renderSpecs[index][specIndex];
            savedSpecs.push_back({
                {"name", "Spec " + ofToString(specIndex + 1, 2, '0')},
                {"fps", entry.fps},
                {"mode", entry.mode},
                {"frameOffset", entry.frameOffset},
                {"fadePercent", entry.fadePercent},
                {"flightStrength", entry.flightStrength},
                {"trailSpeedFactor", entry.trailSpeedFactor}
            });
        }
        document["videos"].push_back({
            {"source", sources[index].label},
            {"input", sources[index].path},
            {"specs", savedSpecs}
        });
    }

    const auto specsPath = appDataPath("render-specs.json");
    std::filesystem::create_directories(specsPath.parent_path());
    if (!ofSavePrettyJson(specsPath, document)) {
        renderSpecs[selectedVideo].pop_back();
        selectedSpecIndices[selectedVideo] = renderSpecs[selectedVideo].size() - 1;
        renderStatus = "Could not save render specs";
        return false;
    }
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
    frameHistory.clear();
    latestVideoFrame.clear();
    previousVideoFrame.clear();
    motionMaskPixels.clear();
    accumulatedInvertedFrame.clear();
    displacementMapPixels.clear();
    displacedVideoPixels.clear();
    displacementField.release();
    travellingPixels.clear();
    hasLatestVideoFrame = false;
    hasProcessedFrame = false;
    invertedLayerInitialized = false;
    selectedVideo = index;
    if (renderSpecs.size() == sources.size() && !renderSpecs[index].empty()) {
        selectedSpecIndices[index] = renderSpecs[index].size() - 1;
        const RenderSpec &spec = renderSpecs[index][selectedSpecIndices[index]];
        frameOffset = spec.frameOffset;
        pixelFadePercent = spec.fadePercent;
        pixelFlightStrength = spec.flightStrength;
        trailSpeedFactor = spec.trailSpeedFactor;
        layerMode = layerModeFromName(spec.mode);
    } else {
        const RenderSpec defaults;
        selectedSpecIndices[index] = 0;
        frameOffset = defaults.frameOffset;
        pixelFadePercent = defaults.fadePercent;
        pixelFlightStrength = defaults.flightStrength;
        trailSpeedFactor = defaults.trailSpeedFactor;
    }
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
    ofSetFrameRate(static_cast<int>(std::lround(targetFrameRate(selectedVideo))));
}

void ofApp::update() {
    player.update();
    const bool sourceFrameChanged = player.isLoaded() && player.isFrameNew();
    if (sourceFrameChanged) {
        const ofPixels &pixels = player.getPixels();
        if (!pixels.isAllocated() || pixels.getNumChannels() != 3) {
            return;
        }
        latestVideoFrame = pixels;
        hasLatestVideoFrame = true;
    }

    if (!hasLatestVideoFrame || (!useSixtyFps && !sourceFrameChanged)) {
        return;
    }

    const ofPixels &pixels = latestVideoFrame;

    frameHistory.push_back(pixels);
    while (frameHistory.size() > static_cast<std::size_t>(frameOffset + 1)) {
        frameHistory.pop_front();
    }

    if (layerMode == LayerMode::MotionMask) {
        if (updateMotionFrame()) {
            uploadProcessedPixels(motionMaskPixels);
        }
    } else if (layerMode != LayerMode::OriginalVideo) {
        updateInvertedFrame(pixels, sourceFrameChanged);
        uploadSelectedInvertedLayer();
    }
}

bool ofApp::updateMotionFrame() {
    if (frameHistory.size() <= static_cast<std::size_t>(frameOffset)) {
        if (!frameHistory.empty()) {
            const ofPixels &current = frameHistory.back();
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

    const ofPixels &current = frameHistory.back();
    const ofPixels &delayed = frameHistory[frameHistory.size() - 1 - frameOffset];
    const int width = static_cast<int>(current.getWidth());
    const int height = static_cast<int>(current.getHeight());

    cv::Mat currentFrame(height, width, CV_8UC3,
                         const_cast<unsigned char *>(current.getData()));
    cv::Mat delayedFrame(height, width, CV_8UC3,
                         const_cast<unsigned char *>(delayed.getData()));
    cv::Mat difference;
    cv::Mat grayscaleDifference;
    cv::Mat motionMask;
    cv::Mat binaryDifference;
    cv::absdiff(currentFrame, delayedFrame, difference);
    cv::cvtColor(difference, grayscaleDifference, cv::COLOR_RGB2GRAY);
    cv::threshold(grayscaleDifference, motionMask, 24, 255, cv::THRESH_BINARY);
    cv::cvtColor(motionMask, binaryDifference, cv::COLOR_GRAY2RGB);

    ofPixels output;
    output.allocate(width, height, OF_PIXELS_RGB);
    std::memcpy(output.getData(), binaryDifference.data,
                binaryDifference.total() * binaryDifference.elemSize());
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
        std::memcpy(displacedVideoPixels.getData(), pixels.getData(), pixels.size());
        previousVideoFrame = pixels;
        travellingPixels.clear();
        invertedLayerInitialized = true;
        return;
    }

    cv::Mat flow;
    cv::Mat movingPixels;
    if (sourceFrameChanged) {
        cv::Mat previousFrame(height, width, CV_8UC3,
                              const_cast<unsigned char *>(previousVideoFrame.getData()));
        cv::Mat previousGrayscale;
        cv::Mat previousFlowFrame;
        cv::Mat currentFlowFrame;
        cv::Mat lowResolutionFlow;
        cv::Mat difference;
        cv::Mat grayscaleDifference;
        cv::absdiff(currentFrame, previousFrame, difference);
        cv::cvtColor(difference, grayscaleDifference, cv::COLOR_RGB2GRAY);
        cv::threshold(grayscaleDifference, movingPixels, 24, 255, cv::THRESH_BINARY);
        cv::cvtColor(previousFrame, previousGrayscale, cv::COLOR_RGB2GRAY);
        const cv::Size flowSize(std::max(1, width / 2), std::max(1, height / 2));
        cv::resize(previousGrayscale, previousFlowFrame, flowSize, 0, 0, cv::INTER_AREA);
        cv::resize(grayscale, currentFlowFrame, flowSize, 0, 0, cv::INTER_AREA);
        cv::calcOpticalFlowFarneback(previousFlowFrame, currentFlowFrame, lowResolutionFlow,
                                     0.5, 3, 15, 3, 5, 1.2, 0);
        cv::resize(lowResolutionFlow, flow, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);
        flow *= 2.0f;
    }

    const float fadeFactor = 1.0f - pixelFadePercent / 100.0f;
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
            const cv::Vec2f *flowRow = sourceFrameChanged ? flow.ptr<cv::Vec2f>(y) : nullptr;
            const unsigned char *movingRow = sourceFrameChanged
                                                 ? movingPixels.ptr<unsigned char>(y)
                                                 : nullptr;
            for (int x = 0; x < width; ++x) {
                cv::Vec2f &offset = fieldRow[x];
                offset[0] *= fadeFactor;
                offset[1] *= fadeFactor;
                if (sourceFrameChanged && movingRow[x] != 0) {
                    const cv::Vec2f movement = flowRow[x];
                    if (std::isfinite(movement[0]) && std::isfinite(movement[1])) {
                        offset[0] += movement[0] * pixelFlightStrength;
                        offset[1] += movement[1] * pixelFlightStrength;
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

    // Traces only feed the inverted grayscale layer; other layers skip the simulation.
    if (layerMode == LayerMode::InvertedGrayscale) {
        updateTravellingPixels(grayscale, flow, movingPixels, sourceFrameChanged);
    }
    if (sourceFrameChanged) {
        previousVideoFrame = pixels;
    }
}

void ofApp::updateTravellingPixels(const cv::Mat &grayscale, const cv::Mat &flow,
                                   const cv::Mat &movingPixels, bool sourceFrameChanged) {
    const int width = grayscale.cols;
    const int height = grayscale.rows;
    cv::Mat inverted;
    cv::bitwise_not(grayscale, inverted);
    cv::Mat accumulatedFrame(height, width, CV_8UC3,
                             accumulatedInvertedFrame.getData());
    const float fadeFactor = 1.0f - pixelFadePercent / 100.0f;

    if (fadeFactor < 1.0f) {
        accumulatedFrame.convertTo(accumulatedFrame, CV_8UC3, fadeFactor);
    }
    const float halfLifeFrames = static_cast<float>(std::max(frameOffset, 1));
    const float speedDecay = std::pow(0.5f, 1.0f / halfLifeFrames);
    constexpr float stopSpeed = 0.035f;
    constexpr float maxTrailLength = 48.0f;

    auto drawStreak = [&](const TravellingPixel &traveller) {
        const float baseTrailLength = std::min(maxTrailLength,
                                               traveller.initialSpeed * halfLifeFrames);
        const float speedAdjustment = std::exp(-trailSpeedFactor * traveller.initialSpeed);
        const float trailLength = std::clamp(baseTrailLength * speedAdjustment,
                                             0.0f, maxTrailLength);
        const int steps = std::max(1, static_cast<int>(std::ceil(trailLength)));
        for (int step = 0; step <= steps; ++step) {
            const float distance = trailLength * step / steps;
            const float streakX = traveller.x - traveller.directionX * distance;
            const float streakY = traveller.y - traveller.directionY * distance;
            if (!std::isfinite(streakX) || !std::isfinite(streakY) ||
                streakX < 0 || streakX >= width || streakY < 0 || streakY >= height) {
                continue;
            }
            const int x = static_cast<int>(std::lround(streakX));
            const int y = static_cast<int>(std::lround(streakY));
            if (x < 0 || x >= width || y < 0 || y >= height) {
                continue;
            }

            const float strength = 1.0f - distance / (trailLength + 1.0f);
            const int brightness = static_cast<int>(traveller.brightness * strength);
            unsigned char *pixel = accumulatedFrame.ptr<unsigned char>(y) + x * 3;
            for (int channel = 0; channel < 3; ++channel) {
                pixel[channel] = static_cast<unsigned char>(
                    std::min(255, static_cast<int>(pixel[channel]) + brightness));
            }
        }
    };

    for (auto &traveller : travellingPixels) {
        traveller.x += traveller.velocityX * pixelFlightStrength;
        traveller.y += traveller.velocityY * pixelFlightStrength;
        traveller.velocityX *= speedDecay;
        traveller.velocityY *= speedDecay;
        drawStreak(traveller);

        if (std::hypot(traveller.velocityX, traveller.velocityY) < stopSpeed ||
            traveller.x < 0 || traveller.x >= width ||
            traveller.y < 0 || traveller.y >= height) {
            traveller.brightness = 0;
        }
    }
    travellingPixels.erase(
        std::remove_if(travellingPixels.begin(), travellingPixels.end(),
                       [](const TravellingPixel &traveller) {
                           return traveller.brightness == 0;
                       }),
        travellingPixels.end());

    if (sourceFrameChanged) {
        const std::size_t availableSlots = maxTravellingPixels - travellingPixels.size();
        std::vector<TravellingPixel> sampledTravellers;
        sampledTravellers.reserve(std::min<std::size_t>(availableSlots, 65536));
        std::size_t eligiblePixels = 0;

        if (availableSlots > 0) {
            for (int y = 0; y < height; ++y) {
                const cv::Vec2f *flowRow = flow.ptr<cv::Vec2f>(y);
                const unsigned char *brightnessRow = inverted.ptr<unsigned char>(y);
                const unsigned char *movingRow = movingPixels.ptr<unsigned char>(y);
                for (int x = 0; x < width; ++x) {
                    if (movingRow[x] == 0 || brightnessRow[x] == 0) {
                        continue;
                    }

                    const cv::Vec2f movement = flowRow[x];
                    const float magnitude = std::hypot(movement[0], movement[1]);
                    if (magnitude < 0.5f) {
                        continue;
                    }

                    const float directionX = movement[0] / magnitude;
                    const float directionY = movement[1] / magnitude;
                    const float speed = magnitude * brightnessRow[x] / 255.0f;
                    if (speed < stopSpeed) {
                        continue;
                    }

                    const TravellingPixel traveller = {
                        static_cast<float>(x) - movement[0],
                        static_cast<float>(y) - movement[1],
                        directionX * speed,
                        directionY * speed,
                        directionX,
                        directionY,
                        speed,
                        brightnessRow[x]
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

        for (const TravellingPixel &traveller : sampledTravellers) {
            travellingPixels.push_back(traveller);
            drawStreak(traveller);
        }
    }
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
    hasProcessedFrame = true;
}

void ofApp::draw() {
    ofBackground(12, 14, 18);
    ofSetColor(255);

    const float maxWidth = ofGetWidth() - 80;
    const float maxHeight = ofGetHeight() - 275;
    const float videoWidth = player.isLoaded() ? player.getWidth() : 0;
    const float videoHeight = player.isLoaded() ? player.getHeight() : 0;

    if (videoWidth > 0 && videoHeight > 0) {
        const float scale = std::min(maxWidth / videoWidth, maxHeight / videoHeight);
        const float drawWidth = videoWidth * scale;
        const float drawHeight = videoHeight * scale;
        const float x = (ofGetWidth() - drawWidth) * 0.5f;
        const float y = 28 + (maxHeight - drawHeight) * 0.5f;

        if (layerMode == LayerMode::OriginalVideo) {
            player.draw(x, y, drawWidth, drawHeight);
        } else if (hasProcessedFrame) {
            processedTexture.draw(x, y, drawWidth, drawHeight);
        }
    }

    ofSetColor(235);
    std::string layerName;
    switch (layerMode) {
    case LayerMode::MotionMask:
        layerName = "Motion threshold";
        break;
    case LayerMode::InvertedGrayscale:
        layerName = "Inverted grayscale, travelling pixel traces";
        break;
    case LayerMode::DisplacementMap:
        layerName = "Displacement map (R=X, G=Y, 128=neutral)";
        break;
    case LayerMode::DisplacedVideo:
        layerName = "Displaced original video";
        break;
    case LayerMode::OriginalVideo:
        layerName = "Original video";
        break;
    }
    const std::string fpsLabel = useSixtyFps
                                     ? "60 FPS"
                                     : "Source FPS " + ofToString(sources[selectedVideo].averageFps, 2);
    std::string traceCount = "Traces: " + ofToString(travellingPixels.size()) +
                             " / " + ofToString(maxTravellingPixels);
    if (travellingPixels.size() >= maxTravellingPixels) {
        traceCount += " FULL";
    }
    ofDrawBitmapString("" + ofToString(selectedVideo + 1) + "  " + sources[selectedVideo].label +
                           "   |   " + layerName + "   |   Offset: " +
                           ofToString(frameOffset) + " frames   |   " + fpsLabel +
                           "   |   Saved specs: " +
                           ofToString(savedSpecCount(selectedVideo)),
                       28, ofGetHeight() - 185);
    ofDrawBitmapString(traceCount, ofGetWidth() - 320, ofGetHeight() - 158);
    ofSetColor(155);
    drawPixelControls();
    ofDrawBitmapString("1-6: select video     Left / Right: select layer     Up / Down: offset     F: source / 60 FPS     make render: render queue",
                       28, ofGetHeight() - 28);
    if (!renderStatus.empty()) {
        ofSetColor(235);
        ofDrawBitmapString(renderStatus, 28, 24);
    }
}

void ofApp::keyPressed(int key) {
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

    if (key >= '1' && key <= '6') {
        loadVideo(static_cast<std::size_t>(key - '1'));
        return;
    }

    if (key == OF_KEY_LEFT || key == OF_KEY_RIGHT) {
        const int layerCount = 5;
        const int currentLayer = static_cast<int>(layerMode);
        const int direction = key == OF_KEY_RIGHT ? 1 : -1;
        const int nextLayer = (currentLayer + direction + layerCount) % layerCount;
        layerMode = static_cast<LayerMode>(nextLayer);
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
        frameHistory.clear();
        hasProcessedFrame = false;
        return;
    }

    if (key == OF_KEY_UP && frameOffset < 120) {
        ++frameOffset;
        frameHistory.clear();
        hasProcessedFrame = false;
    } else if (key == OF_KEY_DOWN && frameOffset > 0) {
        --frameOffset;
        frameHistory.clear();
        hasProcessedFrame = false;
    }
}

void ofApp::drawPixelControls() {
    const float y = ofGetHeight() - 135;
    fadeValueField.set(205, y + 2, 95, 27);
    flightStrengthField.set(440, y + 2, 95, 27);
    trailSpeedFactorField.set(700, y + 2, 105, 27);
    saveSpecsButton.set(850, y + 2, 125, 27);

    ofSetColor(235);
    ofDrawBitmapString("Pixel fade (% / frame)", 28, y + 20);
    ofDrawBitmapString("Flight strength", 320, y + 20);
    ofDrawBitmapString("Trail speed factor", 540, y + 20);

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
                       28, ofGetHeight() - 60);
}

void ofApp::mousePressed(int x, int y, int button) {
    if (button != OF_MOUSE_BUTTON_LEFT) {
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
    frameHistory.clear();
    motionMaskPixels.clear();
    previousVideoFrame.clear();
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
            updateInvertedFrame(sourceFrame, tick == 0);
            if (layerMode == LayerMode::MotionMask) {
                frameHistory.push_back(sourceFrame);
                while (frameHistory.size() > static_cast<std::size_t>(frameOffset + 1)) {
                    frameHistory.pop_front();
                }
                updateMotionFrame();
            }

            const ofPixels *output = &accumulatedInvertedFrame;
            if (layerMode == LayerMode::MotionMask) {
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
    std::filesystem::path specsPath = appDataPath("render-specs.json");
    // Each run gets its own folder, so earlier videos are never overwritten.
    std::filesystem::path outputDirectory =
        appDataPath("renders/inverted-motion-trails/video/" +
                    ofGetTimestampString("%Y%m%d-%H%M%S"));
    std::string only;

    auto usage = []() {
        ofLogError("render")
            << "usage: helloOpenCv --render [--jobs N] [--frames N] [--only video[/spec]]"
               " [--specs file.json] [--out directory]";
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

    if (!std::filesystem::exists(specsPath)) {
        ofLogError("render") << "render queue not found: " << specsPath;
        return 1;
    }
    ofApp queue;
    queue.loadRenderSpecs(specsPath);
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
    std::filesystem::copy_file(specsPath, outputDirectory / "render-specs.json",
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

    const auto startTime = std::chrono::steady_clock::now();
    auto secondsSinceStart = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime)
            .count();
    };
    std::atomic<std::size_t> nextJob{0};
    std::atomic<std::size_t> finishedJobs{0};
    std::vector<std::thread> workers;
    for (std::size_t worker = 0; worker < jobCount; ++worker) {
        workers.emplace_back([&]() {
            // Each worker owns a separate processing state.
            ofApp renderer;
            renderer.renderSpecs = queue.renderSpecs;
            for (std::size_t job = nextJob++; job < jobs.size(); job = nextJob++) {
                RenderProgress &jobProgress = progress[job];
                jobProgress.startSeconds = secondsSinceStart();
                jobProgress.state = RenderState::Running;
                const bool rendered = renderer.renderJob(jobs[job].first, jobs[job].second,
                                                         outputDirectory, maxFrames, jobProgress);
                jobProgress.endSeconds = secondsSinceStart();
                jobProgress.state = rendered ? RenderState::Done : RenderState::Failed;
                ++finishedJobs;
            }
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
