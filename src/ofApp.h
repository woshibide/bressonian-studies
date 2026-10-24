#pragma once

#include "ofMain.h"

#include <deque>
#include <string>
#include <vector>

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    enum class LayerMode {
        MotionMask,
        InvertedGrayscale
    };

    struct VideoSource {
        std::string label;
        std::string path;
        float averageFps;
    };

    void loadVideo(std::size_t index);
    void updateMotionFrame();
    void updateInvertedFrame(const ofPixels &pixels);
    void uploadProcessedPixels(const ofPixels &pixels);

    std::vector<VideoSource> sources;
    ofVideoPlayer player;
    ofTexture processedTexture;
    std::deque<ofPixels> frameHistory;
    ofPixels previousVideoFrame;
    ofPixels accumulatedInvertedFrame;
    std::size_t selectedVideo = 0;
    int frameOffset = 3;
    LayerMode layerMode = LayerMode::MotionMask;
    bool hasProcessedFrame = false;
    bool invertedLayerInitialized = false;
};
