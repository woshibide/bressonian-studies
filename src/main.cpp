#include "ofMain.h"
#include "ofApp.h"
#include "ofAppGLFWWindow.h"

namespace {

// No update callback: playback and simulation belong exclusively to the controls app.
class VideoWindow : public ofBaseApp {
public:
    explicit VideoWindow(std::shared_ptr<ofApp> app) : app(std::move(app)) {}
    void setup() override {
        ofSetWindowTitle("CCTV Video");
        ofSetVerticalSync(false);
    }
    void draw() override { app->drawVideo(); }
    void exit() override { ofExit(); }
private:
    std::shared_ptr<ofApp> app;
};

} // namespace

int main(int argc, char *argv[]) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0] == "--render") {
        return ofApp::runRenderCommand(args);
    }

    ofGLFWWindowSettings settings;
    settings.setGLVersion(2, 1);
    settings.setSize(1100, 400);
    settings.setPosition(glm::vec2(30, 40));
    settings.windowMode = OF_WINDOW;
    auto controls = ofCreateWindow(settings);

    settings.setSize(960, 540);
    settings.setPosition(glm::vec2(180, 470));
    settings.shareContextWith = controls;
    auto video = ofCreateWindow(settings);

    auto app = std::make_shared<ofApp>();
    ofRunApp(controls, app);
    ofRunApp(video, std::make_shared<VideoWindow>(app));
    ofRunMainLoop();
}
