#include "ofApp.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <random>

namespace {

constexpr std::size_t maxTravellingPixels = 250000;

of::filesystem::path appDataPath(const of::filesystem::path &relativePath) {
    return (ofFilePath::getCurrentExeDirFS() / "../../../data" / relativePath)
        .lexically_normal();
}

} // namespace

void ofApp::setup() {
    ofSetWindowTitle("CCTV Motion Difference");
    ofSetWindowShape(1100, 720);
    ofSetVerticalSync(false);

    sources = {
        {"Cercany", "fps-normalized/cercany-native-cadence.mp4", 15.73f},
        {"Daegu", "fps-normalized/daegu-native-cadence.mp4", 15.04f},
        {"Korea", "fps-normalized/korea-native-cadence.mp4", 15.00f},
        {"Meagan", "fps-normalized/meagan-native-cadence.mp4", 27.59f},
        {"Norway", "fps-normalized/norway-native-cadence.mp4", 45.44f}
    };

    loadRenderSpecs();

    loadVideo(0);
}

void ofApp::loadRenderSpecs() {
    renderSpecs = {
        {{3, 2.50f, 1000.0f, 60.0f, "invertedGrayscale"}},
        {{3, 100.0f, -1.0f, 60.0f, "invertedGrayscale"}},
        {{3, 75.0f, -50.0f, 60.0f, "invertedGrayscale"}},
        {{15, 100.0f, -50.0f, 60.0f, "invertedGrayscale"}},
        {{1, 99.0f, -1.0f, 60.0f, "invertedGrayscale"}}
    };
    selectedSpecIndices.assign(sources.size(), 0);

    const auto specsPath = appDataPath("render-specs.json");
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
            renderSpecs[index].clear();
            for (const auto &savedSpec : entry["specs"]) {
                if (savedSpec.is_object()) {
                    renderSpecs[index].push_back(readSpec(savedSpec, RenderSpec{}));
                }
            }
            if (renderSpecs[index].empty()) {
                renderSpecs[index].push_back(RenderSpec{});
            }
        } else {
            const RenderSpec legacySpec = readSpec(entry, renderSpecs[index].front());
            const RenderSpec &defaultSpec = renderSpecs[index].front();
            if (legacySpec.frameOffset != defaultSpec.frameOffset ||
                legacySpec.fadePercent != defaultSpec.fadePercent ||
                legacySpec.flightStrength != defaultSpec.flightStrength ||
                legacySpec.fps != defaultSpec.fps || legacySpec.mode != defaultSpec.mode) {
                renderSpecs[index].push_back(legacySpec);
            }
        }
        selectedSpecIndices[index] = renderSpecs[index].size() - 1;
    }
}

std::size_t ofApp::savedSpecCount(std::size_t videoIndex) const {
    return videoIndex < renderSpecs.size() ? renderSpecs[videoIndex].size() : 0;
}

bool ofApp::saveRenderSpecs() {
    if (renderSpecs.size() != sources.size() || selectedVideo >= renderSpecs.size()) {
        return false;
    }

    RenderSpec spec;
    spec.frameOffset = frameOffset;
    spec.fadePercent = pixelFadePercent;
    spec.flightStrength = pixelFlightStrength;
    spec.trailSpeedFactor = trailSpeedFactor;
    spec.fps = 60.0f;
    spec.mode = "invertedGrayscale";
    renderSpecs[selectedVideo].push_back(spec);
    selectedSpecIndices[selectedVideo] = renderSpecs[selectedVideo].size() - 1;

    ofJson document;
    document["version"] = 1;
    document["videos"] = ofJson::array();
    for (std::size_t index = 0; index < sources.size(); ++index) {
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
    accumulatedInvertedFrame.clear();
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
    if (batchRendering) {
        updateBatchRender();
        return;
    }

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
        updateMotionFrame();
    } else if (layerMode == LayerMode::InvertedGrayscale) {
        updateInvertedFrame(pixels, sourceFrameChanged);
    }
}

void ofApp::updateMotionFrame() {
    if (frameHistory.size() <= static_cast<std::size_t>(frameOffset)) {
        return;
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
    uploadProcessedPixels(output);
}

void ofApp::updateInvertedFrame(const ofPixels &pixels, bool sourceFrameChanged) {
    const int width = static_cast<int>(pixels.getWidth());
    const int height = static_cast<int>(pixels.getHeight());
    cv::Mat currentFrame(height, width, CV_8UC3,
                         const_cast<unsigned char *>(pixels.getData()));
    cv::Mat grayscale;
    cv::Mat inverted;
    cv::cvtColor(currentFrame, grayscale, cv::COLOR_RGB2GRAY);
    cv::bitwise_not(grayscale, inverted);

    if (!invertedLayerInitialized || !accumulatedInvertedFrame.isAllocated() ||
        accumulatedInvertedFrame.getWidth() != width ||
        accumulatedInvertedFrame.getHeight() != height) {
        accumulatedInvertedFrame.allocate(width, height, OF_PIXELS_RGB);
        std::memset(accumulatedInvertedFrame.getData(), 0,
                    accumulatedInvertedFrame.size());
        previousVideoFrame = pixels;
        travellingPixels.clear();
        invertedLayerInitialized = true;
        uploadProcessedPixels(accumulatedInvertedFrame);
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
        static std::mt19937_64 sampler{0x6f70656e4657ULL};

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
                            std::uniform_int_distribution<std::size_t>(0, eligiblePixels)(sampler);
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
        previousVideoFrame = pixels;
    }

    uploadProcessedPixels(accumulatedInvertedFrame);
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
    const std::string layerName = layerMode == LayerMode::MotionMask
                                      ? "Motion threshold"
                                      : layerMode == LayerMode::InvertedGrayscale
                                            ? "Inverted grayscale, travelling pixel traces"
                                            : "Original video";
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
    ofDrawBitmapString("1-5: select video     Left / Right: select layer     Up / Down: offset     F: source / 60 FPS     R: render batch",
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

    if (key == 'r' && !batchRendering) {
        startBatchRender();
        return;
    }

    if (key >= '1' && key <= '5') {
        loadVideo(static_cast<std::size_t>(key - '1'));
        return;
    }

    if (key == OF_KEY_LEFT || key == OF_KEY_RIGHT) {
        const int layerCount = 3;
        const int currentLayer = static_cast<int>(layerMode);
        const int direction = key == OF_KEY_RIGHT ? 1 : -1;
        const int nextLayer = (currentLayer + direction + layerCount) % layerCount;
        layerMode = static_cast<LayerMode>(nextLayer);
        hasProcessedFrame = false;
        if (layerMode == LayerMode::InvertedGrayscale) {
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

void ofApp::startBatchRender() {
    batchRendering = true;
    renderStatus = "Preparing batch renders";
    renderVideoIndex = 0;
    renderSpecIndex = 0;
    layerMode = LayerMode::InvertedGrayscale;
    ofSetFrameRate(0);

    const std::filesystem::path outputDirectory =
        appDataPath("renders/inverted-motion-trails");
    std::filesystem::create_directories(outputDirectory);

    while (renderVideoIndex < sources.size() &&
           !startRenderVideo(renderVideoIndex, renderSpecIndex)) {
        advanceBatchRender();
    }
    if (renderVideoIndex >= sources.size()) {
        batchRendering = false;
        applyTargetFrameRate();
    }
}

bool ofApp::startRenderVideo(std::size_t videoIndex, std::size_t specIndex) {
    renderCapture.release();
    if (videoIndex >= sources.size() || videoIndex >= renderSpecs.size() ||
        specIndex >= renderSpecs[videoIndex].size()) {
        return false;
    }

    selectedVideo = videoIndex;
    renderSpecIndex = specIndex;
    selectedSpecIndices[videoIndex] = specIndex;
    const RenderSpec &spec = renderSpecs[videoIndex][specIndex];
    frameOffset = spec.frameOffset;
    pixelFadePercent = spec.fadePercent;
    pixelFlightStrength = spec.flightStrength;
    trailSpeedFactor = spec.trailSpeedFactor;
    renderedFrameCount = 0;
    renderPendingTicks = 0;
    renderTickRemainder = 0;
    renderSourceFps = sources[videoIndex].averageFps;
    renderOutputFps = spec.fps;
    frameHistory.clear();
    renderSourceFrame.clear();
    previousVideoFrame.clear();
    accumulatedInvertedFrame.clear();
    travellingPixels.clear();
    invertedLayerInitialized = false;
    hasProcessedFrame = false;

    if (!renderCapture.open(appDataPath(sources[videoIndex].path).string())) {
        ofLogError("ofApp") << "Could not open render input: " << sources[videoIndex].path;
        renderStatus = "Could not open render input: " + sources[videoIndex].label;
        return false;
    }

    const int width = static_cast<int>(renderCapture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(renderCapture.get(cv::CAP_PROP_FRAME_HEIGHT));
    const std::string name = ofToLower(sources[videoIndex].label);
    const std::string specFolder = "spec-" +
                                   ofToString(static_cast<int>(specIndex + 1), 2, '0');
    renderFramesDirectory =
        appDataPath("renders/inverted-motion-trails/frames/" + name + "/" +
                    specFolder).string();
    std::filesystem::remove_all(renderFramesDirectory);
    std::filesystem::create_directories(renderFramesDirectory);
    renderGrayFrame.allocate(width, height, OF_PIXELS_GRAY);

    renderStatus = "Exporting " + sources[videoIndex].label + " spec " +
                   ofToString(specIndex + 1) + " PNG frames at " +
                   ofToString(width) + "x" + ofToString(height) +
                   ", " + ofToString(renderOutputFps, 2) + " FPS, offset " +
                   ofToString(frameOffset);
    ofLogNotice("ofApp") << "Rendering " << sources[videoIndex].label << " spec "
                          << (specIndex + 1) << " at " << renderOutputFps
                          << " fps with offset " << frameOffset;
    return true;
}

void ofApp::advanceBatchRender() {
    if (renderVideoIndex >= renderSpecs.size()) {
        return;
    }
    ++renderSpecIndex;
    if (renderSpecIndex >= renderSpecs[renderVideoIndex].size()) {
        ++renderVideoIndex;
        renderSpecIndex = 0;
    }
}

void ofApp::updateBatchRender() {
    bool sourceFrameChanged = false;
    if (renderPendingTicks == 0) {
        cv::Mat bgrFrame;
        if (!renderCapture.read(bgrFrame)) {
            renderCapture.release();
            ofLogNotice("ofApp") << "Finished " << sources[renderVideoIndex].label << ": "
                                  << renderedFrameCount << " frames";
            renderStatus = "Finished " + sources[renderVideoIndex].label + ": " +
                           " spec " + ofToString(renderSpecIndex + 1) + ": " +
                           ofToString(renderedFrameCount) + " frames";
            advanceBatchRender();
            while (renderVideoIndex < sources.size() &&
                   !startRenderVideo(renderVideoIndex, renderSpecIndex)) {
                advanceBatchRender();
            }
            if (renderVideoIndex >= sources.size()) {
                batchRendering = false;
                player.setPosition(0);
                player.play();
                applyTargetFrameRate();
                ofLogNotice("ofApp") << "Batch render complete";
                renderStatus = "Batch render complete";
            }
            return;
        }

        cv::Mat rgbFrame;
        cv::cvtColor(bgrFrame, rgbFrame, cv::COLOR_BGR2RGB);
        renderSourceFrame.setFromPixels(rgbFrame.data, rgbFrame.cols, rgbFrame.rows,
                                        OF_PIXELS_RGB);
        renderTickRemainder += renderOutputFps / renderSourceFps;
        renderPendingTicks = static_cast<int>(std::floor(renderTickRemainder));
        renderTickRemainder -= renderPendingTicks;
        if (renderPendingTicks == 0) {
            return;
        }
        sourceFrameChanged = true;
    }

    updateInvertedFrame(renderSourceFrame, sourceFrameChanged);
    --renderPendingTicks;

    cv::Mat accumulatedFrame(static_cast<int>(renderSourceFrame.getHeight()),
                             static_cast<int>(renderSourceFrame.getWidth()), CV_8UC3,
                             accumulatedInvertedFrame.getData());
    cv::Mat grayscaleOutput;
    cv::extractChannel(accumulatedFrame, grayscaleOutput, 0);
    renderGrayFrame.setFromPixels(grayscaleOutput.data, grayscaleOutput.cols,
                                  grayscaleOutput.rows, OF_PIXELS_GRAY);

    const std::string framePath = renderFramesDirectory + "/frame-" +
                                  ofToString(renderedFrameCount, 6, '0') + ".png";
    if (!ofSaveImage(renderGrayFrame, framePath, OF_IMAGE_QUALITY_BEST)) {
        renderStatus = "Could not save frame " + ofToString(renderedFrameCount) +
                       " for " + sources[renderVideoIndex].label;
        renderCapture.release();
        batchRendering = false;
        return;
    }
    ++renderedFrameCount;
    renderStatus = "Rendering " + sources[renderVideoIndex].label + " at offset " +
                   ofToString(frameOffset) + ": " + ofToString(renderedFrameCount) + " frames";
}
