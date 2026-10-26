#pragma once

#include "ParticleCanvas.h"
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

    // How the displaced video layer turns the displacement field into pixels.
    enum class PixelTreatment {
        Smear,
        Streak,
        Stretch
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
        std::string treatment = "smear";
        bool colouredParticles = false;
        bool particlesOnly = false;
        bool boids = false;
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
        // Zero marks a traveller for removal.
        unsigned char brightness;
        float originX;
        float originY;
        cv::Vec3b colour;
        // Starts at 1 when the traveller leaves its origin and drops by the pixel fade each frame.
        float opacity;
    };

    void discoverVideos();
    void loadVideo(std::size_t index);
    void pushSourceFrame(const ofPixels &pixels);
    bool updateMotionFrame();
    void updateInvertedFrame(const ofPixels &pixels, bool sourceFrameChanged);
    void updateTravellingPixels(const cv::Mat &grayscale, const cv::Mat &currentFrame,
                                bool motionMeasured, bool injectMotion);
    float fadeFactor() const;
    LayerMode layerModeFromName(const std::string &name) const;
    std::string layerModeName() const;
    static PixelTreatment pixelTreatmentFromName(const std::string &name);
    static std::string pixelTreatmentName(PixelTreatment treatment);
    ofRectangle treatmentOptionRect(std::size_t index) const;
    void drawTreatmentMenu();
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

    std::vector<VideoSource> sources;
    std::vector<std::vector<RenderSpec>> renderSpecs;
    std::vector<std::size_t> selectedSpecIndices;
    ofVideoPlayer player;
    ofTexture processedTexture;
    ofPixels latestVideoFrame;
    ofPixels motionMaskPixels;
    // The last frameOffset + 1 source frames; optical flow compares the oldest with the newest.
    std::deque<ofPixels> sourceFrameHistory;
    ofPixels accumulatedInvertedFrame;
    ofPixels displacementMapPixels;
    ofPixels displacedVideoPixels;
    cv::Mat displacementField;
    std::vector<TravellingPixel> travellingPixels;
    ParticleCanvas particleCanvas;
    // Sources run slower than the output, so motion measured on one source frame is fed in
    // evenly over the output frames until the next one instead of all at once.
    cv::Mat pendingFlow;
    cv::Mat pendingMovingPixels;
    std::vector<TravellingPixel> pendingTravellers;
    int motionSpreadTicks = 1;
    int motionSpreadTick = 0;
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
    ofRectangle treatmentField;
    bool treatmentMenuOpen = false;
    ofRectangle particlesToggle;
    // Displacement layers push coloured particles instead of pulling through the field.
    bool colouredParticles = false;
    ofRectangle particlesOnlyToggle;
    // With coloured particles, the displaced video shows the particles on black.
    bool particlesOnly = false;
    ofRectangle boidsToggle;
    // Travellers flock and swirl; the tuning lives in ParticleTuning.h.
    bool boids = false;
    FloatInput editingFloatInput = FloatInput::None;
    std::string floatInputText;
    std::string renderStatus;
    bool useSixtyFps = false;
    LayerMode layerMode = LayerMode::MotionMask;
    PixelTreatment pixelTreatment = PixelTreatment::Smear;
    bool hasProcessedFrame = false;
    bool invertedLayerInitialized = false;
    bool hasLatestVideoFrame = false;
};
