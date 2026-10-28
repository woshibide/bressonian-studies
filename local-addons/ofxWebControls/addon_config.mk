meta:
    ADDON_NAME = ofxWebControls
    ADDON_DESCRIPTION = Local HTTP and WebSocket controls using the installed Poco binary
    ADDON_AUTHOR = helloOpenCv
    ADDON_TAGS = network

common:
    ADDON_INCLUDES += ../../../addons/ofxPoco/libs/poco/include

osx:
    ADDON_LIBS += ../../../addons/ofxPoco/libs/poco/lib/macos/poco.xcframework/macos-arm64_x86_64/poco.a
