#pragma once
// Minimal hand-written OpenGL 3.3 core loader: only the entry points GlRenderer uses, resolved
// through a caller-supplied getProcAddress (SDL_GL_GetProcAddress via ui::Window::glProc).
#include <cstddef>
#include <string>

#ifdef _WIN32
#define TF_GLCALL __stdcall
#else
#define TF_GLCALL
#endif

namespace tf::gfx::gl {

using GLenum = unsigned;
using GLuint = unsigned;
using GLint = int;
using GLsizei = int;
using GLbitfield = unsigned;
using GLboolean = unsigned char;
using GLfloat = float;
using GLchar = char;
using GLsizeiptr = ptrdiff_t;

inline constexpr GLenum COLOR_BUFFER_BIT = 0x4000, TRIANGLES = 0x0004, BLEND = 0x0BE2, SCISSOR_TEST = 0x0C11,
                        SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303, ONE = 1, TEXTURE_2D = 0x0DE1,
                        UNSIGNED_BYTE = 0x1401, FLOAT = 0x1406, RED = 0x1903, RGBA = 0x1908, R8 = 0x8229,
                        RGBA8 = 0x8058, TEXTURE_MIN_FILTER = 0x2801, TEXTURE_MAG_FILTER = 0x2800,
                        TEXTURE_WRAP_S = 0x2802, TEXTURE_WRAP_T = 0x2803, LINEAR = 0x2601, CLAMP_TO_EDGE = 0x812F,
                        UNPACK_ROW_LENGTH = 0x0CF2, UNPACK_ALIGNMENT = 0x0CF5, PACK_ALIGNMENT = 0x0D05,
                        ARRAY_BUFFER = 0x8892, STREAM_DRAW = 0x88E0, VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30,
                        COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82, INFO_LOG_LENGTH = 0x8B84, FRAMEBUFFER = 0x8D40,
                        COLOR_ATTACHMENT0 = 0x8CE0, FRAMEBUFFER_COMPLETE = 0x8CD5, TEXTURE0 = 0x84C0, VERSION = 0x1F02,
                        RENDERER = 0x1F01, NO_ERROR = 0;

#define TF_GL_FUNCS(X)                                                                                 \
  X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                                  \
  X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                            \
  X(void, Clear, (GLbitfield))                                                                         \
  X(void, Enable, (GLenum))                                                                            \
  X(void, Disable, (GLenum))                                                                           \
  X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                         \
  X(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                                   \
  X(void, GenTextures, (GLsizei, GLuint*))                                                             \
  X(void, DeleteTextures, (GLsizei, const GLuint*))                                                    \
  X(void, BindTexture, (GLenum, GLuint))                                                               \
  X(void, ActiveTexture, (GLenum))                                                                     \
  X(void, TexParameteri, (GLenum, GLenum, GLint))                                                      \
  X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))    \
  X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
  X(void, PixelStorei, (GLenum, GLint))                                                                \
  X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))                         \
  X(void, GenBuffers, (GLsizei, GLuint*))                                                              \
  X(void, DeleteBuffers, (GLsizei, const GLuint*))                                                     \
  X(void, BindBuffer, (GLenum, GLuint))                                                                \
  X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                                       \
  X(void, GenVertexArrays, (GLsizei, GLuint*))                                                         \
  X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                                \
  X(void, BindVertexArray, (GLuint))                                                                   \
  X(void, EnableVertexAttribArray, (GLuint))                                                           \
  X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))               \
  X(GLuint, CreateShader, (GLenum))                                                                    \
  X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))                         \
  X(void, CompileShader, (GLuint))                                                                     \
  X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                                       \
  X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                      \
  X(void, DeleteShader, (GLuint))                                                                      \
  X(GLuint, CreateProgram, ())                                                                         \
  X(void, AttachShader, (GLuint, GLuint))                                                              \
  X(void, LinkProgram, (GLuint))                                                                       \
  X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                                      \
  X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                     \
  X(void, UseProgram, (GLuint))                                                                        \
  X(void, DeleteProgram, (GLuint))                                                                     \
  X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                                \
  X(void, Uniform1i, (GLint, GLint))                                                                   \
  X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                                        \
  X(void, DrawArrays, (GLenum, GLint, GLsizei))                                                        \
  X(void, GenFramebuffers, (GLsizei, GLuint*))                                                         \
  X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                                \
  X(void, BindFramebuffer, (GLenum, GLuint))                                                           \
  X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                               \
  X(GLenum, CheckFramebufferStatus, (GLenum))                                                          \
  X(GLenum, GetError, ())                                                                              \
  X(const unsigned char*, GetString, (GLenum))

#define TF_GL_DECL(ret, name, args) extern ret(TF_GLCALL* name) args;
TF_GL_FUNCS(TF_GL_DECL)
#undef TF_GL_DECL

// Resolves every entry point above. Returns false (and names the first missing one in *err) on failure.
bool load(void* (*getProc)(const char*), std::string* err = nullptr);

}  // namespace tf::gfx::gl
