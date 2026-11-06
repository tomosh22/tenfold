#include "wrapper.hpp"

namespace media {

Codec::/*@Codec_ctor_def*/Codec(int level) {
    codec_opts opts = { level, /*@use_cpp_notify_cb*/cpp_notify, nullptr };
    handle_ = /*@use_codec_new*/codec_new(&opts);
}

Codec::~Codec() { /*@use_codec_free*/codec_free(handle_); }

int Codec::/*@Codec_run_def*/run(const char *text) {
    ++/*@use_calls_field*/calls_;
    return /*@use_codec_encode*/codec_encode(handle_, text);
}

}  // namespace media

extern "C" int /*@cpp_only_c_def*/cpp_only_c(int v) { return v * 2; }
