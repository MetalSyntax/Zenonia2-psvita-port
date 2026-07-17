/*
 * image_load.c
 *
 * See image_load.h. Only PNG decode is needed (logo.png/title.png/touch.png
 * from apk_extract/res/drawable), so the unused stb_image codecs/SIMD paths
 * are compiled out to keep this small on-device.
 */
#include <stdlib.h>

#include "image_load.h"

extern void game_log(const char *fmt, ...);

#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

// Backward-mapped bilinear resample of a `sw`x`sh` RGBA8888 image into a
// `dw`x`dh` destination, scaling by `scale` and centering/cropping any
// overflow -- i.e. a "cover" fit when scale == max(dw/sw, dh/sh), or a plain
// scale (no crop) when dw/dh already match round(sw*scale)/round(sh*scale).
static void cover_resize_rgba(const unsigned char *src, int sw, int sh,
                               unsigned char *dst, int dw, int dh, float scale) {
    float off_x = (sw * scale - dw) / 2.0f;
    float off_y = (sh * scale - dh) / 2.0f;

    for (int y = 0; y < dh; y++) {
        float sy = (y + off_y) / scale;
        int y0 = (int)sy;
        if (y0 < 0) y0 = 0;
        if (y0 >= sh) y0 = sh - 1;
        int y1 = y0 + 1 < sh ? y0 + 1 : y0;
        float fy = sy - (int)sy;

        for (int x = 0; x < dw; x++) {
            float sx = (x + off_x) / scale;
            int x0 = (int)sx;
            if (x0 < 0) x0 = 0;
            if (x0 >= sw) x0 = sw - 1;
            int x1 = x0 + 1 < sw ? x0 + 1 : x0;
            float fx = sx - (int)sx;

            const unsigned char *p00 = &src[(y0 * sw + x0) * 4];
            const unsigned char *p10 = &src[(y0 * sw + x1) * 4];
            const unsigned char *p01 = &src[(y1 * sw + x0) * 4];
            const unsigned char *p11 = &src[(y1 * sw + x1) * 4];
            unsigned char *out = &dst[(y * dw + x) * 4];

            for (int c = 0; c < 4; c++) {
                float top = p00[c] + (p10[c] - p00[c]) * fx;
                float bot = p01[c] + (p11[c] - p01[c]) * fx;
                out[c] = (unsigned char)(top + (bot - top) * fy + 0.5f);
            }
        }
    }
}

GLuint image_load_png_tex(const char *path, int dst_w, int dst_h, float scale) {
    int sw, sh, channels;
    unsigned char *src = stbi_load(path, &sw, &sh, &channels, 4);
    if (!src) {
        game_log("image_load: %s no encontrado o PNG invalido (%s)\n", path, stbi_failure_reason());
        return 0;
    }

    if (scale <= 0.0f) {
        scale = (float)dst_w / (float)sw;
        float scale_y = (float)dst_h / (float)sh;
        if (scale_y > scale) scale = scale_y;
    }

    unsigned char *dst = malloc((size_t)dst_w * dst_h * 4);
    if (!dst) {
        stbi_image_free(src);
        game_log("image_load: sin memoria para %s (%dx%d)\n", path, dst_w, dst_h);
        return 0;
    }

    cover_resize_rgba(src, sw, sh, dst, dst_w, dst_h, scale);
    stbi_image_free(src);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dst_w, dst_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, dst);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    free(dst);
    return tex;
}
