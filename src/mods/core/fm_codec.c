/*
 * fm_codec.c - the file formats mods use (fm_mods.h): PNG in and
 * out (stb_image, stb_image_write), raw deflate for the archive reader
 * (stb_image's zlib decoder), and sounds: OGG Vorbis streamed from the file
 * as it plays (stb_vorbis) and WAV PCM, both mixed down to mono.
 *
 * Loop points: an OGG's LOOPSTART comment (samples), a WAV's smpl chunk.
 * stb (external/stb) is MIT or public domain; it is compiled here, static,
 * so no other copy in the program can clash with it.
 */
#include "mods/core/fm_mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

uint32_t *fm_png_decode(const uint8_t *data, size_t size, int *w, int *h)
{
    int comp;
    if (size > 0x7FFFFFFF) {
        return NULL;
    }
    unsigned char *rgba = stbi_load_from_memory(data, (int)size, w, h, &comp, 4);
    if (!rgba) {
        return NULL;
    }
    uint32_t *argb = malloc((size_t)*w * (size_t)*h * sizeof *argb);
    if (argb) {
        for (size_t i = 0, n = (size_t)*w * (size_t)*h; i < n; i++) {
            const unsigned char *p = rgba + i * 4;
            argb[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        }
    }
    stbi_image_free(rgba);
    return argb;
}

static void write_to_file(void *context, void *data, int size)
{
    fwrite(data, 1, (size_t)size, (FILE *)context);
}

bool fm_png_write(const char *path, const uint32_t *argb, int w, int h)
{
    unsigned char *rgba = malloc((size_t)w * (size_t)h * 4);
    if (!rgba) {
        return false;
    }
    for (size_t i = 0, n = (size_t)w * (size_t)h; i < n; i++) {
        rgba[i * 4] = (unsigned char)(argb[i] >> 16);
        rgba[i * 4 + 1] = (unsigned char)(argb[i] >> 8);
        rgba[i * 4 + 2] = (unsigned char)argb[i];
        rgba[i * 4 + 3] = (unsigned char)(argb[i] >> 24);
    }
    FILE *f = fopen(path, "wb");
    bool ok = f && stbi_write_png_to_func(write_to_file, f, w, h, 4, rgba, w * 4) != 0;
    if (f) {
        ok = fclose(f) == 0 && ok;
    }
    free(rgba);
    return ok;
}

uint8_t *fm_inflate(const uint8_t *data, size_t packed, size_t size)
{
    if (packed > 0x7FFFFFFF || size > 0x7FFFFFFE) {
        return NULL;
    }
    uint8_t *out = malloc(size + 1);
    if (!out) {
        return NULL;
    }
    int n = stbi_zlib_decode_noheader_buffer((char *)out, (int)size, (const char *)data, (int)packed);
    if (n != (int)size) {
        free(out);
        return NULL;
    }
    return out;
}

struct FmSound {
    stb_vorbis *ogg;
    float *pcm;    /* WAV: mono samples */
    size_t frames; /* WAV: sample count */
    size_t at;     /* WAV: next sample */
    int rate;
    int channels; /* OGG: the file's */
    long loop_start;
};

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static FmSound *open_wav(const uint8_t *d, size_t size, char *err, size_t cap)
{
    const uint8_t *fmt = NULL, *data = NULL;
    size_t data_size = 0;
    long loop = -1;
    for (size_t at = 12; at + 8 <= size;) {
        uint32_t n = le32(d + at + 4);
        if (n > size - at - 8) {
            n = (uint32_t)(size - at - 8);
        }
        if (!memcmp(d + at, "fmt ", 4) && n >= 16) {
            fmt = d + at + 8;
        } else if (!memcmp(d + at, "data", 4)) {
            data = d + at + 8, data_size = n;
        } else if (!memcmp(d + at, "smpl", 4) && n >= 36 + 24 && le32(d + at + 8 + 28) > 0) {
            loop = (long)le32(d + at + 8 + 36 + 8);
        }
        at += 8 + (size_t)n + (n & 1);
    }
    if (!fmt || !data) {
        snprintf(err, cap, "WAV without a fmt or data chunk.");
        return NULL;
    }
    unsigned format = le16(fmt), channels = le16(fmt + 2), bits = le16(fmt + 14);
    if (format != 1 || (bits != 8 && bits != 16) || channels < 1 || channels > 2) {
        snprintf(err, cap, "WAV must be PCM, 8 or 16-bit, mono or stereo (this one: format %u, %u-bit, %u channels).",
                 format, bits, channels);
        return NULL;
    }
    unsigned rate = le32(fmt + 4);
    if (rate < 8000 || rate > 192000) {
        snprintf(err, cap, "WAV sample rate %u Hz is outside 8000-192000.", rate);
        return NULL;
    }
    size_t step = channels * (bits / 8), frames = data_size / step;
    FmSound *s = calloc(1, sizeof *s);
    float *pcm = malloc((frames ? frames : 1) * sizeof *pcm);
    if (!s || !pcm) {
        free(s);
        free(pcm);
        snprintf(err, cap, "Out of memory.");
        return NULL;
    }
    for (size_t i = 0; i < frames; i++) {
        float sum = 0;
        for (unsigned c = 0; c < channels; c++) {
            const uint8_t *p = data + i * step + c * (bits / 8);
            sum += bits == 8 ? (p[0] - 128) / 128.0f : (int16_t)le16(p) / 32768.0f;
        }
        pcm[i] = sum / (float)channels;
    }
    s->pcm = pcm;
    s->frames = frames;
    s->rate = (int)rate;
    s->loop_start = loop >= 0 && (size_t)loop < frames ? loop : -1;
    return s;
}

static FmSound *open_ogg(const uint8_t *d, size_t size, char *err, size_t cap)
{
    int e = 0;
    if (size > 0x7FFFFFFF) {
        snprintf(err, cap, "OGG file too large.");
        return NULL;
    }
    stb_vorbis *v = stb_vorbis_open_memory(d, (int)size, &e, NULL);
    if (!v) {
        snprintf(err, cap, "Not a readable OGG Vorbis file (stb_vorbis error %d).", e);
        return NULL;
    }
    stb_vorbis_info info = stb_vorbis_get_info(v);
    if (info.channels < 1 || info.sample_rate < 8000 || info.sample_rate > 192000) {
        stb_vorbis_close(v);
        snprintf(err, cap, "OGG with %d channels at %u Hz is not supported.", info.channels, info.sample_rate);
        return NULL;
    }
    FmSound *s = calloc(1, sizeof *s);
    if (!s) {
        stb_vorbis_close(v);
        return NULL;
    }
    s->ogg = v;
    s->rate = (int)info.sample_rate;
    s->channels = info.channels > 8 ? 8 : info.channels;
    s->loop_start = -1;
    stb_vorbis_comment c = stb_vorbis_get_comment(v);
    for (int i = 0; i < c.comment_list_length; i++) {
        if (!_strnicmp(c.comment_list[i], "LOOPSTART=", 10)) {
            s->loop_start = atol(c.comment_list[i] + 10);
        }
    }
    return s;
}

FmSound *fm_sound_open(const uint8_t *file, size_t size, char *err, size_t cap)
{
    if (size >= 12 && !memcmp(file, "RIFF", 4) && !memcmp(file + 8, "WAVE", 4)) {
        return open_wav(file, size, err, cap);
    }
    if (size >= 4 && !memcmp(file, "OggS", 4)) {
        return open_ogg(file, size, err, cap);
    }
    snprintf(err, cap, "Neither an OGG Vorbis nor a WAV file.");
    return NULL;
}

void fm_sound_close(FmSound *s)
{
    if (!s) {
        return;
    }
    if (s->ogg) {
        stb_vorbis_close(s->ogg);
    }
    free(s->pcm);
    free(s);
}

int fm_sound_rate(const FmSound *s)
{
    return s->rate;
}

long fm_sound_loop_start(const FmSound *s)
{
    return s->loop_start;
}

void fm_sound_rewind(FmSound *s)
{
    if (s->ogg) {
        stb_vorbis_seek_start(s->ogg);
    }
    s->at = 0;
}

static size_t read_once(FmSound *s, float *out, size_t count)
{
    if (!s->ogg) {
        size_t n = s->frames - s->at < count ? s->frames - s->at : count;
        memcpy(out, s->pcm + s->at, n * sizeof *out);
        s->at += n;
        return n;
    }
    float block[4096];
    int channels = s->channels;
    size_t done = 0;
    while (done < count) {
        size_t frames = sizeof block / sizeof block[0] / (size_t)channels;
        int want = count - done > frames ? (int)frames : (int)(count - done);
        int got = stb_vorbis_get_samples_float_interleaved(s->ogg, channels, block, want * channels);
        if (got <= 0) {
            break;
        }
        for (int i = 0; i < got; i++) {
            float sum = 0;
            for (int k = 0; k < channels; k++) {
                sum += block[i * channels + k];
            }
            out[done + (size_t)i] = sum / (float)channels;
        }
        done += (size_t)got;
    }
    return done;
}

size_t fm_sound_read(FmSound *s, float *out, size_t count, bool loop)
{
    size_t done = read_once(s, out, count);
    while (loop && done < count) {
        long start = s->loop_start > 0 ? s->loop_start : 0;
        if (s->ogg) {
            stb_vorbis_seek(s->ogg, (unsigned)start);
        } else {
            s->at = (size_t)start;
        }
        size_t got = read_once(s, out + done, count - done);
        if (!got) {
            break;
        }
        done += got;
    }
    return done;
}
