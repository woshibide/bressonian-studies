#include "ofApp.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

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

    loadVideo(0);
}

void ofApp::loadVideo(std::size_t index) {
    if (index >= sources.size()) {
        return;
    }

    player.stop();
    player.close();
    frameHistory.clear();
    hasProcessedFrame = false;
    invertedLayerInitialized = false;
    selectedVideo = index;
    ofSetFrameRate(static_cast<int>(std::lround(sources[selectedVideo].averageFps)));

    player.setPixelFormat(OF_PIXELS_RGB);
    if (!player.load(sources[selectedVideo].path)) {
        ofLogError("ofApp") << "Could not load video: " << sources[selectedVideo].path;
        return;
    }

    player.setLoopState(OF_LOOP_NORMAL);
    player.play();
}

void ofApp::update() {
    player.update();
    if (!player.isLoaded() || !player.isFrameNew()) {
        return;
    }

    const ofPixels &pixels = player.getPixels();
    if (!pixels.isAllocated() || pixels.getNumChannels() != 3) {
        return;
    }

    frameHistory.push_back(pixels);
    while (frameHistory.size() > static_cast<std::size_t>(frameOffset + 1)) {
        frameHistory.pop_front();
    }

    if (layerMode == LayerMode::MotionMask) {
        updateMotionFrame();
    } else {
        updateInvertedFrame(pixels);
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

void ofApp::updateInvertedFrame(const ofPixels &pixels) {
    const int width = static_cast<int>(pixels.getWidth());
    const int height = static_cast<int>(pixels.getHeight());
    cv::Mat currentFrame(height, width, CV_8UC3,
                         const_cast<unsigned char *>(pixels.getData()));
    cv::Mat grayscale;
    cv::Mat inverted;
    cv::Mat invertedRgb;
    cv::cvtColor(currentFrame, grayscale, cv::COLOR_RGB2GRAY);
    cv::bitwise_not(grayscale, inverted);
    cv::cvtColor(inverted, invertedRgb, cv::COLOR_GRAY2RGB);

    if (!invertedLayerInitialized || !accumulatedInvertedFrame.isAllocated() ||
        accumulatedInvertedFrame.getWidth() != width ||
        accumulatedInvertedFrame.getHeight() != height) {
        accumulatedInvertedFrame.allocate(width, height, OF_PIXELS_RGB);
        std::memcpy(accumulatedInvertedFrame.getData(), invertedRgb.data,
                    invertedRgb.total() * invertedRgb.elemSize());
        invertedLayerInitialized = true;
    } else {
        cv::Mat previousFrame(height, width, CV_8UC3,
                              const_cast<unsigned char *>(previousVideoFrame.getData()));
        cv::Mat difference;
        cv::Mat grayscaleDifference;
        cv::Mat updateMask;
        cv::absdiff(currentFrame, previousFrame, difference);
        cv::cvtColor(difference, grayscaleDifference, cv::COLOR_RGB2GRAY);
        cv::threshold(grayscaleDifference, updateMask, 24, 255, cv::THRESH_BINARY);

        cv::Mat accumulatedFrame(height, width, CV_8UC3, accumulatedInvertedFrame.getData());
        invertedRgb.copyTo(accumulatedFrame, updateMask);
    }

    previousVideoFrame = pixels;
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
    const float maxHeight = ofGetHeight() - 160;
    const float videoWidth = player.isLoaded() ? player.getWidth() : 0;
    const float videoHeight = player.isLoaded() ? player.getHeight() : 0;

    if (videoWidth > 0 && videoHeight > 0) {
        const float scale = std::min(maxWidth / videoWidth, maxHeight / videoHeight);
        const float drawWidth = videoWidth * scale;
        const float drawHeight = videoHeight * scale;
        const float x = (ofGetWidth() - drawWidth) * 0.5f;
        const float y = 28 + (maxHeight - drawHeight) * 0.5f;

        if (hasProcessedFrame) {
            processedTexture.draw(x, y, drawWidth, drawHeight);
        }
    }

    ofSetColor(235);
    const std::string layerName = layerMode == LayerMode::MotionMask
                                      ? "Motion threshold"
                                      : "B&W inverted, update changing pixels";
    ofDrawBitmapString("" + ofToString(selectedVideo + 1) + "  " + sources[selectedVideo].label +
                           "   |   " + layerName + "   |   Offset: " +
                           ofToString(frameOffset) + " frames   |   Target FPS: " +
                           ofToString(static_cast<int>(std::lround(sources[selectedVideo].averageFps))),
                       28, ofGetHeight() - 72);
    ofSetColor(155);
    ofDrawBitmapString("1-5: select video     M: motion mask     I: B&W invert     Up / Down: frame offset",
                       28, ofGetHeight() - 48);
}

void ofApp::keyPressed(int key) {
    if (key >= '1' && key <= '5') {
        loadVideo(static_cast<std::size_t>(key - '1'));
        return;
    }

    if (key == 'm') {
        layerMode = LayerMode::MotionMask;
        hasProcessedFrame = false;
        return;
    }
    if (key == 'i') {
        layerMode = LayerMode::InvertedGrayscale;
        hasProcessedFrame = false;
        invertedLayerInitialized = false;
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
