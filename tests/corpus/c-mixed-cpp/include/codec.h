#ifndef CODEC_H
#define CODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#define /*@CODEC_VERSION_def*/CODEC_VERSION 3

/* opaque in both languages; defined in codec.c */
typedef struct /*@codec_tag*/codec /*@codec_td*/codec;

typedef void (*/*@codec_cb_td*/codec_cb)(int code, void *user);

typedef struct {
    int /*@opts_level*/level;
    codec_cb /*@opts_cb*/cb;
    void *user;
} /*@codec_opts_td*/codec_opts;

codec */*@codec_new_decl*/codec_new(const codec_opts *opts);
int /*@codec_encode_decl*/codec_encode(codec *c, const char *text);
void /*@codec_free_decl*/codec_free(codec *c);

/* implemented in C++ (callback_impl.cpp) but called from C (codec.c) */
void /*@cpp_notify_decl*/cpp_notify(int code, void *user);

/* implemented in C (callbacks.c) and called from C++ */
int /*@c_checksum_decl*/c_checksum(const char *text);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif
