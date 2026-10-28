#include "ofMain.h"
#include "ofApp.h"
#include "ofAppGLFWWindow.h"

int main(int argc, char *argv[]) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0] == "--render") {
        return ofApp::runRenderCommand(args);
    }

    ofGLFWWindowSettings settings;
    settings.setGLVersion(2, 1);
    settings.setSize(960, 540);
    settings.setPosition(glm::vec2(180, 120));
    settings.windowMode = OF_WINDOW;
    auto video = ofCreateWindow(settings);
    ofRunApp(video, std::make_shared<ofApp>());
    ofRunMainLoop();
}
