#pragma once

#include "ofMain.h"
#include <opencv2/videoio.hpp>

#include <deque>
#include <string>
#include <vector>

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;

private:
    enum class LayerMode {
        MotionMask,
        InvertedGrayscale,
        OriginalVideo
    };

    enum class FloatInput {
        None,
        PixelFade,
        FlightStrength
    };

    struct VideoSource {
        std::string label;
        std::string path;
        float averageFps;
    };

    struct TravellingPixel {
        float x;
        float y;
        float velocityX;
        float velocityY;
        float directionX;
        float directionY;
        float initialSpeed;
        unsigned char brightness;
    };

    void loadVideo(std::size_t index);
    void updateMotionFrame();
    void updateInvertedFrame(const ofPixels &pixels, bool sourceFrameChanged);
    void uploadProcessedPixels(const ofPixels &pixels);
    float targetFrameRate(std::size_t index) const;
    void applyTargetFrameRate();
    void drawPixelControls();
    void startBatchRender();
    bool startRenderVideo(std::size_t index);
    void updateBatchRender();

    std::vector<VideoSource> sources;
    ofVideoPlayer player;
    ofTexture processedTexture;
    std::deque<ofPixels> frameHistory;
    ofPixels latestVideoFrame;
    ofPixels previousVideoFrame;
    ofPixels accumulatedInvertedFrame;
    ofPixels renderSourceFrame;
    ofPixels renderGrayFrame;
    std::vector<TravellingPixel> travellingPixels;
    cv::VideoCapture renderCapture;
    std::size_t renderVideoIndex = 0;
    std::size_t renderedFrameCount = 0;
    int renderPendingTicks = 0;
    double renderTickRemainder = 0;
    float renderSourceFps = 0;
    float renderOutputFps = 0;
    std::string renderFramesDirectory;
    std::size_t selectedVideo = 0;
    int frameOffset = 3;
    float pixelFadePercent = 2.5f;
    float pixelFlightStrength = 1.0f;
    ofRectangle fadeValueField;
    ofRectangle flightStrengthField;
    FloatInput editingFloatInput = FloatInput::None;
    std::string floatInputText;
    std::string renderStatus;
    bool useSixtyFps = false;
    LayerMode layerMode = LayerMode::MotionMask;
    bool hasProcessedFrame = false;
    bool invertedLayerInitialized = false;
    bool batchRendering = false;
    bool hasLatestVideoFrame = false;
};
