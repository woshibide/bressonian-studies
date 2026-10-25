#pragma once

#include "ofMain.h"
#include <opencv2/core.hpp>

#include <atomic>
#include <deque>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(int x, int y, int button) override;

    // Renders the JSON queue without a window; returns the process exit code.
    static int runRenderCommand(const std::vector<std::string> &args);

private:
    enum class LayerMode {
        MotionMask,
        InvertedGrayscale,
        DisplacementMap,
        DisplacedVideo,
        OriginalVideo
    };

    enum class FloatInput {
        None,
        PixelFade,
        FlightStrength,
        TrailSpeedFactor
    };

    struct VideoSource {
        std::string label;
        std::string path;
        float averageFps;
    };

    struct RenderSpec {
        int frameOffset = 3;
        float fadePercent = 2.5f;
        float flightStrength = 1.0f;
        float fps = 60.0f;
        std::string mode = "invertedGrayscale";
        float trailSpeedFactor = 0.15f;
    };

    enum class RenderState {
        Queued,
        Running,
        Done,
        Failed
    };

    // Shared between a render worker and the terminal status display.
    struct RenderProgress {
        std::string name;
        std::string mode;
        std::size_t expectedFrames = 0;
        std::atomic<std::size_t> frames{0};
        std::atomic<RenderState> state{RenderState::Queued};
        std::atomic<double> startSeconds{0};
        std::atomic<double> endSeconds{0};
        // Written by the worker before it sets state to Failed.
        std::string error;
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
    bool updateMotionFrame();
    void updateInvertedFrame(const ofPixels &pixels, bool sourceFrameChanged);
    void updateTravellingPixels(const cv::Mat &grayscale, const cv::Mat &flow,
                                const cv::Mat &movingPixels, bool sourceFrameChanged);
    LayerMode layerModeFromName(const std::string &name) const;
    std::string layerModeName() const;
    void uploadSelectedInvertedLayer();
    void uploadProcessedPixels(const ofPixels &pixels);
    float targetFrameRate(std::size_t index) const;
    void applyTargetFrameRate();
    void drawPixelControls();
    std::size_t savedSpecCount(std::size_t videoIndex) const;
    void loadRenderSpecs(const std::filesystem::path &specsPath);
    bool saveRenderSpecs();
    bool renderJob(std::size_t videoIndex, std::size_t specIndex,
                   const std::filesystem::path &outputDirectory, std::size_t maxFrames,
                   RenderProgress &progress);

    std::vector<VideoSource> sources = {
        {"Cercany", "fps-normalized/cercany-native-cadence.mp4", 15.73f},
        {"Daegu", "fps-normalized/daegu-native-cadence.mp4", 15.04f},
        {"Korea", "fps-normalized/korea-native-cadence.mp4", 15.00f},
        {"Meagan", "fps-normalized/meagan-native-cadence.mp4", 27.59f},
        {"Norway", "fps-normalized/norway-native-cadence.mp4", 45.44f},
        {"Flower", "fps-normalized/12860598_960_540_25fps.mp4", 25.00f}
    };
    std::vector<std::vector<RenderSpec>> renderSpecs;
    std::vector<std::size_t> selectedSpecIndices;
    ofVideoPlayer player;
    ofTexture processedTexture;
    std::deque<ofPixels> frameHistory;
    ofPixels latestVideoFrame;
    ofPixels motionMaskPixels;
    ofPixels previousVideoFrame;
    ofPixels accumulatedInvertedFrame;
    ofPixels displacementMapPixels;
    ofPixels displacedVideoPixels;
    cv::Mat displacementField;
    std::vector<TravellingPixel> travellingPixels;
    std::mt19937_64 travellerSampler{0x6f70656e4657ULL};
    std::size_t selectedVideo = 0;
    int frameOffset = 3;
    float pixelFadePercent = 2.5f;
    float pixelFlightStrength = 1.0f;
    float trailSpeedFactor = 0.15f;
    ofRectangle fadeValueField;
    ofRectangle flightStrengthField;
    ofRectangle trailSpeedFactorField;
    ofRectangle saveSpecsButton;
    FloatInput editingFloatInput = FloatInput::None;
    std::string floatInputText;
    std::string renderStatus;
    bool useSixtyFps = false;
    LayerMode layerMode = LayerMode::MotionMask;
    bool hasProcessedFrame = false;
    bool invertedLayerInitialized = false;
    bool hasLatestVideoFrame = false;
};
