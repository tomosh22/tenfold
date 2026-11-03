#include <stdlib.h>
#include "codec.h"

/* the one place that defines the opaque struct */
struct /*@codec_tag_def*/codec {
    codec_opts opts;
    int /*@codec_count*/count;
};

codec */*@codec_new_def*/codec_new(const codec_opts *opts) {
    codec *c = calloc(1, sizeof *c);
    if (opts) c->opts = *opts;
    return c;
}

int /*@codec_encode_def*/codec_encode(codec *c, const char *text) {
    int sum = /*@use_c_checksum_in_c*/c_checksum(text);
    c->/*@use_count_c*/count += 1;
    if (c->opts./*@use_opts_cb_c*/cb) c->opts./*@use_opts_cb_c2*/cb(sum, c->opts.user);
    /*@use_cpp_notify_in_c*/cpp_notify(sum, c->opts.user);
    return sum + /*@use_version_c*/CODEC_VERSION;
}

void /*@codec_free_def*/codec_free(codec *c) { free(c); }
