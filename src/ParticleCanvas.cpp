// openFrameworks draws with OpenGL, which macOS marks deprecated but still supports.
#define GL_SILENCE_DEPRECATION

#include "ParticleCanvas.h"

#include <stdexcept>

ParticleCanvas::~ParticleCanvas() {
    // GL objects belong to one context; without it current, deleting them is invalid.
    if (owner != nullptr && CGLGetCurrentContext() == owner) {
        release();
    }
}

void ParticleCanvas::release() {
    if (framebuffer != 0) {
        glDeleteFramebuffers(1, &framebuffer);
        framebuffer = 0;
    }
    if (texture != 0) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
    width = 0;
    height = 0;
}

void ParticleCanvas::allocate(int newWidth, int newHeight) {
    release();
    owner = CGLGetCurrentContext();
    if (owner == nullptr) {
        throw std::runtime_error("ParticleCanvas needs a current GL context");
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, newWidth, newHeight, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("ParticleCanvas framebuffer is incomplete");
    }
    width = newWidth;
    height = newHeight;
}

void ParticleCanvas::draw(cv::Mat &image, const std::vector<Vertex> &vertices,
                          GLenum primitive, Blend blend) {
    if (image.type() != CV_8UC3 || !image.isContinuous()) {
        throw std::invalid_argument("ParticleCanvas draws onto continuous CV_8UC3 images");
    }
    if (vertices.empty()) {
        return;
    }

    // Leave openFrameworks' GL state as it was.
    GLint previousFramebuffer = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();

    if (image.cols != width || image.rows != height || CGLGetCurrentContext() != owner) {
        allocate(image.cols, image.rows);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    // Image row y becomes framebuffer row y, so no flip is needed on the way back.
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE,
                    image.data);
    glBindTexture(GL_TEXTURE_2D, 0);

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, 0, height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    // Pixel (x, y) covers [x, x + 1); shift so integer positions land on pixel centres.
    glTranslatef(0.5f, 0.5f, 0.0f);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_POINT_SMOOTH);
    glDisable(GL_MULTISAMPLE);
    glDisable(GL_DITHER);
    glLineWidth(1.0f);
    glPointSize(1.0f);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    if (blend == Blend::Add) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(Vertex), &vertices[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &vertices[0].red);
    glDrawArrays(primitive, 0, static_cast<GLsizei>(vertices.size()));

    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, image.data);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
}
