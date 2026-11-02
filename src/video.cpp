#include "video.h"
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>
#include "settings.h"
#include "util.h"

extern const char* g_xbrShaderSource;  // shaders_xbr.cpp
extern const char* g_ffxA;             // shaders_fsr.cpp (AMD FidelityFX FSR 1.0)
extern const char* g_ffxFsr1;
extern "C" void ue_fsr_easu_con(unsigned c[16], float vw, float vh, float tw, float th, float ow, float oh);
extern "C" void ue_fsr_rcas_con(unsigned c[4], float sharpness);

static std::string fsrEasuFS() {
    return std::string("#version 430 core\n#define A_GPU 1\n#define A_GLSL 1\n") + g_ffxA +
           "\nuniform uvec4 Con0; uniform uvec4 Con1; uniform uvec4 Con2; uniform uvec4 Con3;\n"
           "uniform sampler2D Source; uniform float FlipY; uniform float OutH;\n"
           "#define FSR_EASU_F 1\n"
           "AF4 FsrEasuRF(AF2 p){ return textureGather(Source, p, 0); }\n"
           "AF4 FsrEasuGF(AF2 p){ return textureGather(Source, p, 1); }\n"
           "AF4 FsrEasuBF(AF2 p){ return textureGather(Source, p, 2); }\n" +
           g_ffxFsr1 +
           "\nout vec4 FragColor;\n"
           "void main(){ vec2 fc = gl_FragCoord.xy; if (FlipY > 0.5) fc.y = OutH - fc.y;\n"
           "  AF3 c; FsrEasuF(c, AU2(fc), Con0, Con1, Con2, Con3); FragColor = vec4(c, 1.0); }\n";
}
static std::string fsrRcasFS() {
    return std::string("#version 430 core\n#define A_GPU 1\n#define A_GLSL 1\n") + g_ffxA +
           "\nuniform uvec4 RCon; uniform sampler2D Inter; uniform vec2 ViewOrigin;\n"
           "#define FSR_RCAS_F 1\n"
           "AF4 FsrRcasLoadF(ASU2 p){ ivec2 s = textureSize(Inter, 0) - 1; return texelFetch(Inter, clamp(ivec2(p), ivec2(0), s), 0); }\n"
           "void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b){}\n" +
           g_ffxFsr1 +
           "\nout vec4 FragColor;\n"
           "void main(){ AF1 r, g, b; FsrRcasF(r, g, b, AU2(gl_FragCoord.xy - ViewOrigin), RCon);\n"
           "  FragColor = vec4(r, g, b, 1.0); }\n";
}

static const char* kVS = R"(#version 330 core
layout(location=0) in vec4 VertexCoord;
layout(location=1) in vec2 TexCoord;
uniform mat4 MVPMatrix;
out vec2 vTex;
void main(){ gl_Position = MVPMatrix * VertexCoord; vTex = TexCoord; }
)";

static const char* kFSPlain = R"(#version 330 core
in vec2 vTex;
out vec4 FragColor;
uniform sampler2D Source;
void main(){ FragColor = vec4(texture(Source, vTex).rgb, 1.0); }
)";

// Bilinear com redução de qualidade (supersampling): quando a resolução interna é bem maior que a
// tela (ex.: 8x numa TV 1080p), faz a média de vários texels em vez de pular pixels. Isso transforma o
// upscale alto em anti-aliasing de verdade e evita o "serrilhado cintilante".
static const char* kFSArea = R"(#version 330 core
in vec2 vTex;
out vec4 FragColor;
uniform sampler2D Source;
uniform vec2 TextureSize;
uniform vec2 InputSize;
uniform vec2 OutputSize;
void main(){
    vec2 ratio = InputSize / OutputSize;
    if (max(ratio.x, ratio.y) <= 1.3) { FragColor = vec4(texture(Source, vTex).rgb, 1.0); return; }
    vec2 n = clamp(ceil(ratio * 0.5), vec2(1.0), vec2(4.0));
    vec2 stepUV = (ratio / n) / TextureSize;
    vec2 lo = vec2(0.5) / TextureSize, hi = InputSize / TextureSize - lo;
    vec2 uvmin = min(lo, hi), uvmax = max(lo, hi);
    vec3 acc = vec3(0.0);
    for (int y = 0; y < 4; y++) {
        if (float(y) >= n.y) break;
        for (int x = 0; x < 4; x++) {
            if (float(x) >= n.x) break;
            vec2 off = (vec2(float(x), float(y)) + 0.5 - n * 0.5) * stepUV;
            acc += texture(Source, clamp(vTex + off, uvmin, uvmax)).rgb;
        }
    }
    FragColor = vec4(acc / (n.x * n.y), 1.0);
}
)";

static const char* kFSSharp = R"(#version 330 core
in vec2 vTex;
out vec4 FragColor;
uniform sampler2D Source;
uniform vec2 TextureSize;
uniform vec2 InputSize;
uniform vec2 OutputSize;
void main(){
    vec2 texel = vTex * TextureSize;
    vec2 scale = max(floor(OutputSize / InputSize), vec2(1.0));
    vec2 tf = floor(texel);
    vec2 s = fract(texel);
    vec2 rr = 0.5 - 0.5 / scale;
    vec2 cd = s - 0.5;
    vec2 f = (cd - clamp(cd, -rr, rr)) * scale + 0.5;
    FragColor = vec4(texture(Source, (tf + f) / TextureSize).rgb, 1.0);
}
)";

static const char* kFSCrt = R"(#version 330 core
in vec2 vTex;
out vec4 FragColor;
uniform sampler2D Source;
uniform vec2 TextureSize;
uniform vec2 InputSize;
uniform vec2 OutputSize;
uniform float NativeLines;
void main(){
    vec2 texel = vTex * TextureSize;
    vec2 scale = max(floor(OutputSize / InputSize), vec2(1.0));
    vec2 tf = floor(texel);
    vec2 s = fract(texel);
    vec2 rr = 0.5 - 0.5 / scale;
    vec2 cd = s - 0.5;
    vec2 f = (cd - clamp(cd, -rr, rr)) * scale + 0.5;
    vec3 col = texture(Source, (tf + f) / TextureSize).rgb;
    col = pow(col, vec3(2.2));
    float line = (vTex.y * TextureSize.y / InputSize.y) * NativeLines;
    float y = fract(line) - 0.5;
    float scan = exp(-y * y * 9.0);
    col *= mix(0.45, 1.25, scan);
    int m = int(mod(gl_FragCoord.x, 3.0));
    vec3 mask = vec3(0.82);
    if (m == 0) mask.r = 1.18; else if (m == 1) mask.g = 1.18; else mask.b = 1.18;
    col *= mask;
    col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2)) * 1.08;
    FragColor = vec4(col, 1.0);
}
)";

GLuint Presenter::compile(const char* vs, const char* fs, bool xbr) {
    auto mk = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096];
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            util::log("Shader: erro de compilacao: %s", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    };
    GLuint v = mk(GL_VERTEX_SHADER, vs), f = mk(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    if (xbr) {
        glBindAttribLocation(p, 0, "VertexCoord");
        glBindAttribLocation(p, 1, "TexCoord");
        glBindAttribLocation(p, 2, "Color");
    }
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        util::log("Shader: erro de link: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool Presenter::init() {
    prog_[FILTER_NEAREST] = compile(kVS, kFSPlain, false);
    prog_[FILTER_BILINEAR] = compile(kVS, kFSArea, false);
    if (!prog_[FILTER_BILINEAR]) prog_[FILTER_BILINEAR] = prog_[FILTER_NEAREST];
    prog_[FILTER_SHARP] = compile(kVS, kFSSharp, false);
    prog_[FILTER_CRT] = compile(kVS, kFSCrt, false);
    std::string xv = std::string("#version 330 core\n#define VERTEX\n") + g_xbrShaderSource;
    std::string xf = std::string("#version 330 core\n#define FRAGMENT\n") + g_xbrShaderSource;
    prog_[FILTER_XBR] = compile(xv.c_str(), xf.c_str(), true);
    if (!prog_[FILTER_XBR]) prog_[FILTER_XBR] = prog_[FILTER_SHARP];
    {   // AMD FSR 1.0: EASU (super resolução espacial) + RCAS (nitidez). Precisa de GL 4.3.
        std::string e = fsrEasuFS(), r = fsrRcasFS();
        easu_ = compile(kVS, e.c_str(), false);
        rcas_ = compile(kVS, r.c_str(), false);
        util::log("Video: AMD FSR 1.0 %s", easu_ && rcas_ ? "disponivel" : "indisponivel (GL 4.3 necessario)");
    }
    prog_[FILTER_FSR] = prog_[FILTER_BILINEAR];
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenTextures(1, &swTex_);
    return prog_[FILTER_NEAREST] && prog_[FILTER_SHARP];
}

bool Presenter::ensureInter(int w, int h) {
    if (interFbo_ && interW_ == w && interH_ == h) return true;
    if (interFbo_) glDeleteFramebuffers(1, &interFbo_);
    if (interTex_) glDeleteTextures(1, &interTex_);
    glGenTextures(1, &interTex_);
    glBindTexture(GL_TEXTURE_2D, interTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &interFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, interFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, interTex_, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    interW_ = w;
    interH_ = h;
    return ok;
}

void Presenter::drawQuad(GLuint p, GLuint tex, float tw, float th, float u1, float vB, float vT, int vw, int vh,
                         float outW, float outH, float nativeLines, bool linear) {
    float quad[16] = {0, 0, 0, vB, 1, 0, u1, vB, 0, 1, 0, vT, 1, 1, u1, vT};
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glUseProgram(p);
    static const float mvp[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
    GLint l;
    if ((l = glGetUniformLocation(p, "MVPMatrix")) >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, mvp);
    if ((l = glGetUniformLocation(p, "Source")) >= 0) glUniform1i(l, 0);
    if ((l = glGetUniformLocation(p, "Inter")) >= 0) glUniform1i(l, 0);
    if ((l = glGetUniformLocation(p, "decal")) >= 0) glUniform1i(l, 0);
    if ((l = glGetUniformLocation(p, "TextureSize")) >= 0) glUniform2f(l, tw, th);
    if ((l = glGetUniformLocation(p, "InputSize")) >= 0) glUniform2f(l, (float)frameW_, (float)frameH_);
    if ((l = glGetUniformLocation(p, "OutputSize")) >= 0) glUniform2f(l, outW, outH);
    if ((l = glGetUniformLocation(p, "NativeLines")) >= 0) glUniform1f(l, nativeLines > 0 ? nativeLines : (float)frameH_);
    (void)vw;
    (void)vh;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glVertexAttrib4f(2, 1, 1, 1, 1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Presenter::shutdown() {
    if (interFbo_) glDeleteFramebuffers(1, &interFbo_);
    if (interTex_) glDeleteTextures(1, &interTex_);
    interFbo_ = interTex_ = 0;
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (fboTex_) glDeleteTextures(1, &fboTex_);
    if (fboRb_) glDeleteRenderbuffers(1, &fboRb_);
    if (swTex_) glDeleteTextures(1, &swTex_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    fbo_ = fboTex_ = fboRb_ = swTex_ = vbo_ = vao_ = 0;
}

void Presenter::uploadSoftware(const void* data, unsigned w, unsigned h, size_t pitch, retro_pixel_format fmt) {
    GLenum ifmt = GL_RGBA8, f = GL_BGRA, t = GL_UNSIGNED_INT_8_8_8_8_REV;
    unsigned bpp = 4;
    if (fmt == RETRO_PIXEL_FORMAT_RGB565) { ifmt = GL_RGB8; f = GL_RGB; t = GL_UNSIGNED_SHORT_5_6_5; bpp = 2; }
    else if (fmt == RETRO_PIXEL_FORMAT_0RGB1555) { ifmt = GL_RGBA8; f = GL_BGRA; t = GL_UNSIGNED_SHORT_1_5_5_5_REV; bpp = 2; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, swTex_);
    if (w > swTexW_ || h > swTexH_ || (int)fmt != swFmt_) {
        swTexW_ = w > swTexW_ ? w : swTexW_;
        swTexH_ = h > swTexH_ ? h : swTexH_;
        swFmt_ = (int)fmt;
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt, swTexW_, swTexH_, 0, f, t, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(pitch / bpp));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, f, t, data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    hw_ = false;
    frameW_ = w;
    frameH_ = h;
}

bool Presenter::createFBO(unsigned w, unsigned h, bool depth, bool stencil) {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (fboTex_) glDeleteTextures(1, &fboTex_);
    if (fboRb_) glDeleteRenderbuffers(1, &fboRb_);
    fbo_ = fboTex_ = fboRb_ = 0;
    GLint maxTex = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    if ((GLint)w > maxTex) w = maxTex;
    if ((GLint)h > maxTex) h = maxTex;
    fboW_ = w;
    fboH_ = h;
    glGenTextures(1, &fboTex_);
    glBindTexture(GL_TEXTURE_2D, fboTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex_, 0);
    if (depth || stencil) {
        glGenRenderbuffers(1, &fboRb_);
        glBindRenderbuffer(GL_RENDERBUFFER, fboRb_);
        glRenderbufferStorage(GL_RENDERBUFFER, stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, fboRb_);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    util::log("Video: FBO %ux%u depth=%d stencil=%d status=0x%x", w, h, depth, stencil, st);
    return st == GL_FRAMEBUFFER_COMPLETE;
}

void Presenter::setHwFrame(unsigned w, unsigned h, bool bottomLeft) {
    hw_ = true;
    bottomLeft_ = bottomLeft;
    frameW_ = w > fboW_ ? fboW_ : w;
    frameH_ = h > fboH_ ? fboH_ : h;
}

void Presenter::draw(int winW, int winH, int filter, float aspect, float nativeLines) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, winW, winH);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!frameW_ || !frameH_) return;

    // retângulo de destino
    int dw = winW, dh = winH;
    if (aspect > 0.0f) {
        if ((float)winW / winH > aspect) { dh = winH; dw = (int)std::lround(winH * aspect); }
        else { dw = winW; dh = (int)std::lround(winW / aspect); }
    }
    dstX_ = (winW - dw) / 2;
    dstY_ = (winH - dh) / 2;
    dstW_ = dw;
    dstH_ = dh;
    glViewport(dstX_, dstY_, dw, dh);

    GLuint tex = hw_ ? fboTex_ : swTex_;
    float tw = (float)(hw_ ? fboW_ : swTexW_), th = (float)(hw_ ? fboH_ : swTexH_);
    float u1 = frameW_ / tw, vmax = frameH_ / th;
    bool flip = hw_ ? !bottomLeft_ : true;
    float vB = flip ? vmax : 0.0f, vT = flip ? 0.0f : vmax;
    float quad[16] = {0, 0, 0, vB, 1, 0, u1, vB, 0, 1, 0, vT, 1, 1, u1, vT};

    if (filter == FILTER_FSR && easu_ && rcas_ && ensureInter(dw, dh)) {
        // passo 1: para a textura intermediária no tamanho da tela (EASU ao ampliar, média ao reduzir)
        glBindFramebuffer(GL_FRAMEBUFFER, interFbo_);
        glViewport(0, 0, dw, dh);
        bool up = frameW_ < (unsigned)dw || frameH_ < (unsigned)dh;
        if (up) {
            unsigned c[16];
            ue_fsr_easu_con(c, (float)frameW_, (float)frameH_, tw, th, (float)dw, (float)dh);
            glUseProgram(easu_);
            glUniform4ui(glGetUniformLocation(easu_, "Con0"), c[0], c[1], c[2], c[3]);
            glUniform4ui(glGetUniformLocation(easu_, "Con1"), c[4], c[5], c[6], c[7]);
            glUniform4ui(glGetUniformLocation(easu_, "Con2"), c[8], c[9], c[10], c[11]);
            glUniform4ui(glGetUniformLocation(easu_, "Con3"), c[12], c[13], c[14], c[15]);
            glUniform1f(glGetUniformLocation(easu_, "FlipY"), flip ? 1.0f : 0.0f);
            glUniform1f(glGetUniformLocation(easu_, "OutH"), (float)dh);
            drawQuad(easu_, tex, tw, th, u1, 0.0f, vmax, dw, dh, (float)dw, (float)dh, nativeLines, true);
        } else {
            drawQuad(prog_[FILTER_BILINEAR], tex, tw, th, u1, vB, vT, dw, dh, (float)dw, (float)dh, nativeLines, true);
        }
        // passo 2: RCAS (nitidez adaptativa) para a tela
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(dstX_, dstY_, dw, dh);
        unsigned rc[4];
        ue_fsr_rcas_con(rc, 0.25f);
        glUseProgram(rcas_);
        glUniform4ui(glGetUniformLocation(rcas_, "RCon"), rc[0], rc[1], rc[2], rc[3]);
        glUniform2f(glGetUniformLocation(rcas_, "ViewOrigin"), (float)dstX_, (float)dstY_);
        drawQuad(rcas_, interTex_, (float)dw, (float)dh, 1, 0, 1, dw, dh, (float)dw, (float)dh, nativeLines, false);
        glViewport(0, 0, winW, winH);
        return;
    }
    if (filter == FILTER_FSR) filter = FILTER_BILINEAR;
    if (filter < 0 || filter >= FILTER_COUNT) filter = FILTER_BILINEAR;
    GLuint p = prog_[filter] ? prog_[filter] : prog_[FILTER_NEAREST];
    bool linear = (filter == FILTER_BILINEAR || filter == FILTER_SHARP || filter == FILTER_CRT);

    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);

    glUseProgram(p);
    static const float mvp[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
    auto loc = [&](const char* n) { return glGetUniformLocation(p, n); };
    GLint l;
    if ((l = loc("MVPMatrix")) >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, mvp);
    if ((l = loc("Source")) >= 0) glUniform1i(l, 0);
    if ((l = loc("decal")) >= 0) glUniform1i(l, 0);
    if ((l = loc("TextureSize")) >= 0) glUniform2f(l, tw, th);
    if ((l = loc("InputSize")) >= 0) glUniform2f(l, (float)frameW_, (float)frameH_);
    if ((l = loc("OutputSize")) >= 0) glUniform2f(l, (float)dw, (float)dh);
    if ((l = loc("NativeLines")) >= 0) glUniform1f(l, nativeLines > 0 ? nativeLines : (float)frameH_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glVertexAttrib4f(2, 1, 1, 1, 1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glViewport(0, 0, winW, winH);
}

bool Presenter::screenshot(const std::string& path) {
    if (dstW_ <= 0 || dstH_ <= 0) return false;
    std::vector<uint8_t> px((size_t)dstW_ * dstH_ * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(dstX_, dstY_, dstW_, dstH_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    size_t row = (size_t)dstW_ * 4;
    // BMP 32 bits (BGRA, de baixo para cima)
    uint32_t imgSize = (uint32_t)(row * dstH_);
    uint8_t hdr[54] = {'B', 'M'};
    auto w32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) hdr[o + i] = (v >> (8 * i)) & 0xff; };
    w32(2, 54 + imgSize); w32(10, 54); w32(14, 40); w32(18, (uint32_t)dstW_); w32(22, (uint32_t)dstH_);
    hdr[26] = 1; hdr[28] = 32; w32(34, imgSize);
    for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]);
    std::vector<uint8_t> file(hdr, hdr + 54);
    file.insert(file.end(), px.begin(), px.end());
    bool ok = util::writeFile(path, file.data(), file.size());
    return ok;
}
