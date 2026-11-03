#pragma once
#include "codec.h"

namespace media {

class /*@ICodec*/ICodec {
public:
    virtual ~ICodec() = default;
    virtual int /*@ICodec_run*/run(const char *text) = 0;
};

/* C++ RAII wrapper around the C handle; the class and the C typedef differ in case only */
class /*@Codec*/Codec : public ICodec {
public:
    /*@Codec_ctor*/Codec(int level);
    ~Codec() override;
    int /*@Codec_run*/run(const char *text) override;
    int /*@Codec_calls*/calls() const { return /*@use_calls_getter*/calls_; }
private:
    ::/*@use_codec_td_member*/codec *handle_;
    int /*@calls_field*/calls_ = 0;
};

class /*@NullCodec*/NullCodec : public ICodec {
public:
    int /*@NullCodec_run*/run(const char *) override { return 0; }
};

}  // namespace media

/* C++ declaration of a C function inside the header, also visible to the C++ side only */
extern "C" {
int /*@cpp_only_c_decl*/cpp_only_c(int v);
}
