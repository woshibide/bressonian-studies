#pragma once

#include "ofMain.h"
#include <OpenGL/OpenGL.h>
#include <opencv2/core.hpp>

#include <vector>

// Draws particle points or lines onto an RGB image with OpenGL and reads the result back.
// It uses the fixed-function pipeline, so it works in the app's GL 2.1 window and in the
// offscreen GL 2.1 contexts that headless renders create. The calling thread needs a current
// GL context, and the same context for every call.
class ParticleCanvas {
public:
    struct Vertex {
        float x;
        float y;
        unsigned char red;
        unsigned char green;
        unsigned char blue;
        // Strength of the vertex; colours are interpolated along a line.
        unsigned char alpha;
    };

    enum class Blend {
        // pixel = pixel + (colour - pixel) * alpha
        Cover,
        // pixel = min(1, pixel + colour * alpha)
        Add
    };

    ParticleCanvas() = default;
    ParticleCanvas(const ParticleCanvas &) = delete;
    ParticleCanvas &operator=(const ParticleCanvas &) = delete;
    ~ParticleCanvas();
    // Call with the owning context current, before its window closes.
    void clear() { release(); owner = nullptr; }

    // Vertex positions are pixel indices, with x to the right and y down like the image rows.
    // primitive is GL_POINTS or GL_LINES. image must be CV_8UC3 and is updated in place.
    void draw(cv::Mat &image, const std::vector<Vertex> &vertices, GLenum primitive,
              Blend blend);

private:
    void allocate(int width, int height);
    void release();

    GLuint texture = 0;
    GLuint framebuffer = 0;
    int width = 0;
    int height = 0;
    CGLContextObj owner = nullptr;
};
