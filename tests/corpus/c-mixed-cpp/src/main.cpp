#include <memory>
#include "wrapper.hpp"

static void /*@on_code_def*/on_code(int code, void *) { (void)code; }

int main() {
    using namespace media;
    /*@use_Codec*/Codec c(5);
    int r = c./*@use_Codec_run*/run("abc");
    std::unique_ptr</*@use_ICodec*/ICodec> p(new /*@use_NullCodec*/NullCodec);
    r += p->/*@use_ICodec_run*/run("x");

    /* the C view of the same header from C++ */
    codec_opts o = { 1, /*@use_on_code*/on_code, nullptr };
    /*@use_codec_td_main*/codec *h = /*@use_codec_new_main*/codec_new(&o);
    r += /*@use_codec_encode_main*/codec_encode(h, "q");
    r += /*@use_c_checksum_main*/c_checksum("z") + /*@use_cpp_only_c*/cpp_only_c(2);
    r += /*@use_version*/CODEC_VERSION + o./*@use_level_main*/level + c./*@use_calls*/calls();
    /*@use_codec_free_main*/codec_free(h);
    return r;
}
