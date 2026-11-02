// UniEmu - apresentação de vídeo (FBO para núcleos 3D, textura para 2D, filtros)
#pragma once
#include <string>
#include "gl.h"
#include "libretro.h"

class Presenter {
public:
    bool init();
    void shutdown();

    void uploadSoftware(const void* data, unsigned w, unsigned h, size_t pitch, retro_pixel_format fmt);
    bool createFBO(unsigned w, unsigned h, bool depth, bool stencil);
    GLuint fbo() const { return fbo_; }
    unsigned fboWidth() const { return fboW_; }
    unsigned fboHeight() const { return fboH_; }
    void setHwFrame(unsigned w, unsigned h, bool bottomLeftOrigin);
    bool hasFrame() const { return frameW_ > 0; }
    unsigned frameWidth() const { return frameW_; }
    unsigned frameHeight() const { return frameH_; }

    // aspect <= 0: esticar. nativeLines: linhas originais (para scanlines)
    void draw(int winW, int winH, int filter, float aspect, float nativeLines);
    bool screenshot(const std::string& path);
    bool fsrAvailable() const { return easu_ && rcas_; }

private:
    GLuint compile(const char* vs, const char* fs, bool xbr);
    void drawQuad(GLuint prog, GLuint tex, float tw, float th, float u1, float vB, float vT, int vw, int vh,
                  float outW, float outH, float nativeLines, bool linear);
    bool ensureInter(int w, int h);
    GLuint easu_ = 0, rcas_ = 0;
    GLuint interFbo_ = 0, interTex_ = 0;
    int interW_ = 0, interH_ = 0;
    GLuint prog_[8] = {0};
    GLuint vao_ = 0, vbo_ = 0;
    GLuint swTex_ = 0;
    unsigned swTexW_ = 0, swTexH_ = 0;
    int swFmt_ = -1;
    GLuint fbo_ = 0, fboTex_ = 0, fboRb_ = 0;
    unsigned fboW_ = 0, fboH_ = 0;
    bool hw_ = false, bottomLeft_ = true;
    unsigned frameW_ = 0, frameH_ = 0;
    int dstX_ = 0, dstY_ = 0, dstW_ = 0, dstH_ = 0;
};
