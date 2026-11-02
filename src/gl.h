// UniEmu - carregador mínimo de OpenGL (3.3+)
#pragma once
#include <SDL.h>
#include <SDL_opengl.h>

#define UE_GL_FUNCS(X)                                                           \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                               \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                               \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                     \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)               \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                 \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                         \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                             \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                             \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                       \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                       \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                     \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                     \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                   \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                       \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                             \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                   \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                     \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)                         \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                       \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                     \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                           \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                     \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                   \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                         \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                         \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                           \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                           \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                                           \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                             \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                               \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                               \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                         \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                         \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                         \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                         \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                   \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                       \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)               \
    X(PFNGLVERTEXATTRIB4FPROC, glVertexAttrib4f)                                 \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                   \
    X(PFNGLBINDSAMPLERPROC, glBindSampler)                                       \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                           \
    X(PFNGLUNIFORM4UIPROC, glUniform4ui)

#define UE_DECL(type, name) extern type ue_##name;
UE_GL_FUNCS(UE_DECL)
#undef UE_DECL

#define glGenFramebuffers ue_glGenFramebuffers
#define glBindFramebuffer ue_glBindFramebuffer
#define glFramebufferTexture2D ue_glFramebufferTexture2D
#define glFramebufferRenderbuffer ue_glFramebufferRenderbuffer
#define glCheckFramebufferStatus ue_glCheckFramebufferStatus
#define glDeleteFramebuffers ue_glDeleteFramebuffers
#define glGenRenderbuffers ue_glGenRenderbuffers
#define glBindRenderbuffer ue_glBindRenderbuffer
#define glRenderbufferStorage ue_glRenderbufferStorage
#define glDeleteRenderbuffers ue_glDeleteRenderbuffers
#define glCreateShader ue_glCreateShader
#define glShaderSource ue_glShaderSource
#define glCompileShader ue_glCompileShader
#define glGetShaderiv ue_glGetShaderiv
#define glGetShaderInfoLog ue_glGetShaderInfoLog
#define glCreateProgram ue_glCreateProgram
#define glAttachShader ue_glAttachShader
#define glBindAttribLocation ue_glBindAttribLocation
#define glLinkProgram ue_glLinkProgram
#define glGetProgramiv ue_glGetProgramiv
#define glGetProgramInfoLog ue_glGetProgramInfoLog
#define glDeleteShader ue_glDeleteShader
#define glDeleteProgram ue_glDeleteProgram
#define glUseProgram ue_glUseProgram
#define glGetUniformLocation ue_glGetUniformLocation
#define glUniform1i ue_glUniform1i
#define glUniform1f ue_glUniform1f
#define glUniform2f ue_glUniform2f
#define glUniformMatrix4fv ue_glUniformMatrix4fv
#define glGenVertexArrays ue_glGenVertexArrays
#define glBindVertexArray ue_glBindVertexArray
#define glDeleteVertexArrays ue_glDeleteVertexArrays
#define glGenBuffers ue_glGenBuffers
#define glBindBuffer ue_glBindBuffer
#define glBufferData ue_glBufferData
#define glDeleteBuffers ue_glDeleteBuffers
#define glVertexAttribPointer ue_glVertexAttribPointer
#define glEnableVertexAttribArray ue_glEnableVertexAttribArray
#define glVertexAttrib4f ue_glVertexAttrib4f
#define glActiveTexture ue_glActiveTexture
#define glBindSampler ue_glBindSampler
#define glBlendFuncSeparate ue_glBlendFuncSeparate
#define glUniform4ui ue_glUniform4ui

bool ueLoadGL();  // chamar após criar o contexto
