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

// Decodes the PNG at `path`, resamples it to exactly dst_w x dst_h and
// uploads it as an RGBA8888 GL texture. `scale` selects the resize factor:
//   <= 0.0f  -> auto "cover" fit: scale = max(dst_w/src_w, dst_h/src_h),
//               centered, overflowing axis cropped (used for logo/title,
//               which must fill the 960x544 screen exactly).
//   > 0.0f   -> use this scale factor verbatim instead of deriving one from
//               dst_w/dst_h (used for touch.png, which historically reused
//               title's cover factor rather than its own aspect ratio).
// Returns 0 (and logs) on decode/allocation failure.
GLuint image_load_png_tex(const char *path, int dst_w, int dst_h, float scale);

#endif
