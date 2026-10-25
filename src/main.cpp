#include "ofMain.h"
#include "ofApp.h"

int main(int argc, char *argv[]) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0] == "--render") {
        return ofApp::runRenderCommand(args);
    }

    ofGLWindowSettings settings;
    settings.setSize(800, 450);
    settings.windowMode = OF_WINDOW;

    auto window = ofCreateWindow(settings);
    ofRunApp(window, std::make_shared<ofApp>());
    ofRunMainLoop();
}
