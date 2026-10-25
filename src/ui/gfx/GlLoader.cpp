#include "ui/gfx/GlLoader.h"

namespace tf::gfx::gl {

#define TF_GL_DEF(ret, name, args) ret(TF_GLCALL* name) args = nullptr;
TF_GL_FUNCS(TF_GL_DEF)
#undef TF_GL_DEF

bool load(void* (*getProc)(const char*), std::string* err) {
#define TF_GL_LOAD(ret, name, args)                             \
  name = reinterpret_cast<decltype(name)>(getProc("gl" #name)); \
  if (!name) {                                                  \
    if (err) *err = "missing OpenGL function gl" #name;         \
    return false;                                               \
  }
  TF_GL_FUNCS(TF_GL_LOAD)
#undef TF_GL_LOAD
  return true;
}

}  // namespace tf::gfx::gl
