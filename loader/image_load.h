/*
 * image_load.h
 *
 * PNG -> GL texture helper used for the splash/title/touch overlays (see
 * splash_load() in main.c). Decodes with stb_image and resizes with a
 * "cover" bilinear resample (scale to fill the destination box, crop the
 * overflowing axis) so PNGs sourced straight from apk_extract/res/drawable
 * (deployed on-device under ux0:data/zenonia-2/drawable/) reproduce the
 * pre-baked splash.rgba/title.rgba/touch.rgba this replaces.
 */
#ifndef _IMAGE_LOAD_H_
#define _IMAGE_LOAD_H_

#include <vitaGL.h>

/**
 * @brief Decodes a PNG and uploads it as an RGBA8888 GL texture, resampled
 *        to exactly `dst_w` x `dst_h`.
 *
 * @param path   Filesystem path to the PNG asset.
 * @param dst_w  Destination texture width, in pixels.
 * @param dst_h  Destination texture height, in pixels.
 * @param scale  Resize factor. `<= 0.0f` derives an automatic "cover" fit
 *               (scale = max(dst_w/src_w, dst_h/src_h), centered, with the
 *               overflowing axis cropped; used for logo/title, which must
 *               fill the 960x544 screen exactly). `> 0.0f` uses this factor
 *               verbatim instead of deriving one from dst_w/dst_h.
 * @return GL texture name, or 0 (and logs) on decode/allocation failure.
 * @note Ver docs/loader/image_load.md para el razonamiento de por qué
 *       touch.png usa un scale manual en vez del "cover fit" automático.
 */
GLuint image_load_png_tex(const char *path, int dst_w, int dst_h, float scale);

#endif
