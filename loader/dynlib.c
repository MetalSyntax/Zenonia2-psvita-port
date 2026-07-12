#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdarg.h>

#include <vitaGL.h>
#include "so_util.h"
#include "postprocess.h"

extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);
extern void game_log(const char *fmt, ...);

// Stub for __errno
int* __errno(void) {
    static int dummy_errno = 0;
    return &dummy_errno;
}

// Wrappers for Fixed Point OpenGL (if vitaGL lacks direct GLES1 fixed-point wrappers, this ensures safety)
void glClearColorx_wrapper(int r, int g, int b, int a) {
    glClearColor(r / 65536.0f, g / 65536.0f, b / 65536.0f, a / 65536.0f);
}

void glTexParameterx_wrapper(GLenum target, GLenum pname, int param) {
    glTexParameteri(target, pname, param);
}

void *convert_rgb565_to_rgba8888(const void *pixels, int width, int height) {
    if (!pixels) return NULL;
    uint16_t *src = (uint16_t *)pixels;
    uint8_t *dst = (uint8_t *)malloc(width * height * 4);
    
    uint16_t min_p = 0xFFFF, max_p = 0x0000;
    
    for (int i = 0; i < width * height; i++) {
        uint16_t p = src[i];
        if (p < min_p) min_p = p;
        if (p > max_p) max_p = p;
        
        dst[i*4 + 0] = ((p >> 11) & 0x1F) * 255 / 31; // R
        dst[i*4 + 1] = ((p >> 5) & 0x3F) * 255 / 63;  // G
        dst[i*4 + 2] = (p & 0x1F) * 255 / 31;         // B
        dst[i*4 + 3] = 255;                           // A
    }
    
    static int conv_log = 0;
    if (conv_log < 10) {
        game_log("[GL] convert_rgb565_to_rgba8888: min=%04x max=%04x\n", min_p, max_p);
        conv_log++;
    }
    
    return dst;
}

void glTexImage2D_wrapper(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) {
    static int img_log = 0;
    if (img_log < 10) {
        game_log("[GL] glTexImage2D target=%x intFmt=%x w=%d h=%d format=%x type=%x pixels=%p\n", 
                 target, internalformat, width, height, format, type, pixels);
        img_log++;
    }
    glGetError();
    
    if (format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5) {
        if (pixels && img_log < 20) {
            uint16_t *p = (uint16_t *)pixels;
            game_log("  -> First 4 pixels: %04x %04x %04x %04x\n", p[0], p[1], p[2], p[3]);
        }
        // Unica textura RGB565 del motor: el buffer compuesto 400x240 (subido
        // a un POT via glTexSubImage2D despues). El shader de post-proceso
        // opcional necesita el tamaño real de este POT para su uniform de
        // texel size -- ver postprocess.c.
        postprocess_set_source_size(width, height);
        void *new_pixels = convert_rgb565_to_rgba8888(pixels, width, height);
        glTexImage2D(target, level, GL_RGBA, width, height, border, GL_RGBA, GL_UNSIGNED_BYTE, new_pixels);
        if (new_pixels) free(new_pixels);
    } else {
        glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    }
    
    // Force min/mag filters so the texture isn't treated as incomplete due to missing mipmaps
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) game_log("[GL] glTexImage2D ERROR: %x\n", err);
}

void glTexSubImage2D_wrapper(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) {
    static int subimg_log = 0;
    if (subimg_log < 10) {
        game_log("[GL] glTexSubImage2D target=%x w=%d h=%d format=%x type=%x\n", target, width, height, format, type);
        if (type == GL_UNSIGNED_SHORT_5_6_5 && pixels) {
            uint16_t *p = (uint16_t*)pixels;
            game_log("  -> First 4 pixels: %04x %04x %04x %04x\n", p[0], p[1], p[2], p[3]);
        }
        subimg_log++;
    }
    glGetError();
    
    if (format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5) {
        // w=400 h=240 es la firma unica del blit del compositor (confirmado
        // en log) -- marcarlo para que el proximo glDrawArrays use el shader
        // de post-proceso opcional en vez de fixed-function GL_REPLACE.
        if (width == 400 && height == 240) postprocess_mark_next_draw();
        void *new_pixels = convert_rgb565_to_rgba8888(pixels, width, height);
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, GL_RGBA, GL_UNSIGNED_BYTE, new_pixels);
        if (new_pixels) free(new_pixels);
    } else {
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    }
    
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) game_log("[GL] glTexSubImage2D ERROR: %x\n", err);
}

// vitaGL does not correctly consume GL_FIXED vertex arrays (same class of bug already
// worked around for glClearColorx_wrapper/glTexParameterx_wrapper above). This engine is a
// J2ME-derived port that feeds Q16.16 fixed-point vertex data, so passing GL_FIXED straight
// through leaves vitaGL reading the raw ints as if they were floats -> geometry collapses
// off-frustum and the screen stays blank even though the engine keeps rendering frames.
// glVertexPointer_wrapper defers conversion to glDrawArrays_wrapper below, since only there
// do we know how many vertices actually need converting.
// Defers GL_FIXED attribute conversions to glDrawArrays_wrapper since only there we know the count
static const int32_t *pending_fixed_verts = NULL;
static GLint pending_fixed_size = 0;
static GLsizei pending_fixed_stride = 0;
static GLfloat *fixed_vert_buf = NULL;
static int fixed_vert_buf_cap = 0;

static const int32_t *pending_fixed_colors = NULL;
static GLint pending_fixed_color_size = 0;
static GLsizei pending_fixed_color_stride = 0;
static GLfloat *fixed_color_buf = NULL;
static int fixed_color_buf_cap = 0;

static const int32_t *pending_fixed_texcoords = NULL;
static GLint pending_fixed_texcoord_size = 0;
static GLsizei pending_fixed_texcoord_stride = 0;
static GLfloat *fixed_texcoord_buf = NULL;
static int fixed_texcoord_buf_cap = 0;

void glDrawArrays_wrapper(GLenum mode, GLint first, GLsizei count) {
    static int draw_count = 0;
    if (draw_count < 10) {
        game_log("[GL] glDrawArrays mode=%x first=%d count=%d\n", mode, first, count);
        draw_count++;
    }

    if (pending_fixed_verts) {
        int needed_verts = first + count;
        int needed_floats = needed_verts * pending_fixed_size;
        if (needed_floats > fixed_vert_buf_cap) {
            fixed_vert_buf = (GLfloat *)realloc(fixed_vert_buf, needed_floats * sizeof(GLfloat));
            fixed_vert_buf_cap = needed_floats;
        }
        int stride_elems = pending_fixed_stride > 0 ? pending_fixed_stride / sizeof(int32_t) : pending_fixed_size;
        for (int i = 0; i < needed_verts; i++) {
            const int32_t *src = pending_fixed_verts + i * stride_elems;
            GLfloat *dst = fixed_vert_buf + i * pending_fixed_size;
            for (int c = 0; c < pending_fixed_size; c++) {
                dst[c] = src[c] / 65536.0f;
            }
        }
        glVertexPointer(pending_fixed_size, GL_FLOAT, 0, fixed_vert_buf);
    }

    if (pending_fixed_colors) {
        int needed_verts = first + count;
        int needed_floats = needed_verts * pending_fixed_color_size;
        if (needed_floats > fixed_color_buf_cap) {
            fixed_color_buf = (GLfloat *)realloc(fixed_color_buf, needed_floats * sizeof(GLfloat));
            fixed_color_buf_cap = needed_floats;
        }
        int stride_elems = pending_fixed_color_stride > 0 ? pending_fixed_color_stride / sizeof(int32_t) : pending_fixed_color_size;
        for (int i = 0; i < needed_verts; i++) {
            const int32_t *src = pending_fixed_colors + i * stride_elems;
            GLfloat *dst = fixed_color_buf + i * pending_fixed_color_size;
            for (int c = 0; c < pending_fixed_color_size; c++) {
                dst[c] = src[c] / 65536.0f;
            }
        }
        glColorPointer(pending_fixed_color_size, GL_FLOAT, 0, fixed_color_buf);
    }

    if (pending_fixed_texcoords) {
        int needed_verts = first + count;
        int needed_floats = needed_verts * pending_fixed_texcoord_size;
        if (needed_floats > fixed_texcoord_buf_cap) {
            fixed_texcoord_buf = (GLfloat *)realloc(fixed_texcoord_buf, needed_floats * sizeof(GLfloat));
            fixed_texcoord_buf_cap = needed_floats;
        }
        int stride_elems = pending_fixed_texcoord_stride > 0 ? pending_fixed_texcoord_stride / sizeof(int32_t) : pending_fixed_texcoord_size;
        for (int i = 0; i < needed_verts; i++) {
            const int32_t *src = pending_fixed_texcoords + i * stride_elems;
            GLfloat *dst = fixed_texcoord_buf + i * pending_fixed_texcoord_size;
            for (int c = 0; c < pending_fixed_texcoord_size; c++) {
                dst[c] = src[c] / 65536.0f;
            }
        }
        glTexCoordPointer(pending_fixed_texcoord_size, GL_FLOAT, 0, fixed_texcoord_buf);
    }

    // No-op (devuelve 0) a menos que se haya compilado con
    // ENABLE_POSTPROCESS_SHADER Y este sea el blit del compositor marcado por
    // glTexSubImage2D_wrapper -- en ese caso dibuja ella misma el quad (con
    // su propia geometria, ver postprocess.c) y hay que saltearse el
    // glDrawArrays original de mas abajo.
    if (postprocess_try_draw()) return;

    glDrawArrays(mode, first, count);
}

void glTexEnvf_wrapper(GLenum target, GLenum pname, GLfloat param) {
    game_log("[GL] glTexEnvf target=%x pname=%x param=%f\n", target, pname, param);
    glTexEnvf(target, pname, param);
}

void glEnable_wrapper(GLenum cap) {
    static int enable_log = 0;
    if (enable_log < 20) {
        game_log("[GL] glEnable cap=%x\n", cap);
        enable_log++;
    }
    glEnable(cap);
}

void glDisable_wrapper(GLenum cap) {
    static int disable_log = 0;
    if (disable_log < 20) {
        game_log("[GL] glDisable cap=%x\n", cap);
        disable_log++;
    }
    glDisable(cap);
}

void glTexCoordPointer_wrapper(GLint size, GLenum type, GLsizei stride, const void *pointer) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glTexCoordPointer size=%d type=%x stride=%d pointer=%p\n", size, type, stride, pointer);
        log++;
    }
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    static int log_uvs = 0;
    if (pointer && log_uvs < 20) {
        if (type == GL_FIXED) {
            int32_t *uv = (int32_t *)pointer;
            game_log("  -> UVs Fixed (first 6): (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f)\n",
                     uv[0]/65536.0f, uv[1]/65536.0f, uv[2]/65536.0f, uv[3]/65536.0f, uv[4]/65536.0f, uv[5]/65536.0f);
        } else {
            float *uv = (float *)pointer;
            game_log("  -> UVs Float (first 6): (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f)\n",
                     uv[0], uv[1], uv[2], uv[3], uv[4], uv[5]);
        }
        log_uvs++;
    }
    
    if (type == GL_FIXED) {
        pending_fixed_texcoords = (const int32_t *)pointer;
        pending_fixed_texcoord_size = size;
        pending_fixed_texcoord_stride = stride;
        return;
    }

    pending_fixed_texcoords = NULL;
    glTexCoordPointer(size, type, stride, pointer);
}

void glColorPointer_wrapper(GLint size, GLenum type, GLsizei stride, const void *pointer) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glColorPointer size=%d type=%x stride=%d pointer=%p\n", size, type, stride, pointer);
        log++;
    }
    glEnableClientState(GL_COLOR_ARRAY);
    static int log_colors = 0;
    if (pointer && log_colors < 20) {
        if (type == GL_FIXED) {
            int32_t *c = (int32_t *)pointer;
            game_log("  -> Colors Fixed (first 4): (%d, %d, %d, %d)\n", c[0], c[1], c[2], c[3]);
        } else if (type == GL_UNSIGNED_BYTE) {
            uint8_t *c = (uint8_t *)pointer;
            game_log("  -> Colors UByte (first 4): (%d, %d, %d, %d)\n", c[0], c[1], c[2], c[3]);
        } else {
            float *c = (float *)pointer;
            game_log("  -> Colors Float (first 4): (%.2f, %.2f, %.2f, %.2f)\n", c[0], c[1], c[2], c[3]);
        }
        log_colors++;
    }

    if (type == GL_FIXED) {
        pending_fixed_colors = (const int32_t *)pointer;
        pending_fixed_color_size = size;
        pending_fixed_color_stride = stride;
        return;
    }

    pending_fixed_colors = NULL;
    glColorPointer(size, type, stride, pointer);
}

void glVertexPointer_wrapper(GLint size, GLenum type, GLsizei stride, const void *pointer) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glVertexPointer size=%d type=%x stride=%d pointer=%p\n", size, type, stride, pointer);
        log++;
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    static int log_verts = 0;
    if (pointer && log_verts < 20) {
        if (type == GL_FIXED) {
            int32_t *v = (int32_t *)pointer;
            game_log("  -> Verts Fixed (first 6): (%d, %d) (%d, %d) (%d, %d)\n",
                     v[0], v[1], v[2], v[3], v[4], v[5]);
        } else {
            float *v = (float *)pointer;
            game_log("  -> Verts Float (first 6): (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f)\n",
                     v[0], v[1], v[2], v[3], v[4], v[5]);
        }
        log_verts++;
    }

    if (type == GL_FIXED) {
        pending_fixed_verts = (const int32_t *)pointer;
        pending_fixed_size = size;
        pending_fixed_stride = stride;
        return;
    }

    pending_fixed_verts = NULL;
    glVertexPointer(size, type, stride, pointer);
}

void glEnableClientState_wrapper(GLenum array) {
    static int enable_cs_log = 0;
    if (enable_cs_log < 10) {
        game_log("[GL] glEnableClientState array=%x\n", array);
        enable_cs_log++;
    }
    glEnableClientState(array);
}

void glDisableClientState_wrapper(GLenum array) {
    static int disable_cs_log = 0;
    if (disable_cs_log < 10) {
        game_log("[GL] glDisableClientState array=%x\n", array);
        disable_cs_log++;
    }
    glDisableClientState(array);
}

void glMatrixMode_wrapper(GLenum mode) {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glMatrixMode mode=%x\n", mode);
        log++;
    }
    glMatrixMode(mode);
}

void glLoadIdentity_wrapper() {
    static int log = 0;
    if (log < 10) {
        game_log("[GL] glLoadIdentity\n");
        log++;
    }
    glLoadIdentity();
}

void glOrthof_wrapper(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar) {
    game_log("[GL] glOrthof l=%f r=%f b=%f t=%f n=%f f=%f\n", left, right, bottom, top, zNear, zFar);
    glOrthof(left, right, bottom, top, zNear, zFar);
}

void glViewport_wrapper(GLint x, GLint y, GLsizei width, GLsizei height) {
    game_log("[GL] glViewport x=%d y=%d w=%d h=%d\n", x, y, width, height);
    glViewport(x, y, width, height);
}

// C++ / GCC stubs
void __cxa_begin_cleanup() {}
void __cxa_call_unexpected() {}
int __cxa_guard_acquire(int* g) { return !*(char*)(g); }
void __cxa_guard_release(int* g) { *(char*)g = 1; }
void __cxa_type_match() {}
void __gnu_Unwind_Find_exidx() {}
void __stack_chk_fail() {}
int __stack_chk_guard = 0;

void* malloc_wrapper(size_t size) {
    void* ptr = malloc(size);
    //game_log("[FakeJNI] malloc(%u) -> %p\n", size, ptr);
    if (!ptr) {
        // Fallback or just log
        game_log("[FakeJNI] MALLOC FAILED FOR SIZE %u\n", size);
    }
    return ptr;
}
void free_wrapper(void* ptr) {
    free(ptr);
}
void* calloc_wrapper(size_t n, size_t size) {
    return calloc(n, size);
}
void* realloc_wrapper(void* ptr, size_t size) {
    return realloc(ptr, size);
}

void translate_path(const char* in_path, char* out_path, size_t out_size) {
    if (strncmp(in_path, "ux0:", 4) == 0) {
        strncpy(out_path, in_path, out_size);
        return;
    }
    const char* relative = in_path;
    if (strncmp(in_path, "app0:/", 6) == 0) {
        relative += 6;
    }
    while (*relative == '/') {
        relative++;
    }
    snprintf(out_path, out_size, "ux0:data/zenonia-2/assets/%s", relative);
}

FILE* fopen_hook(const char* path, const char* mode) {
    char new_path[256];
    translate_path(path, new_path, sizeof(new_path));
    game_log("[FakeJNI] fopen_hook: %s -> %s\n", path, new_path);
    return fopen(new_path, mode);
}

// struct stat con el layout de bionic (Android ARM 32-bit, NDK android-9) --
// NO es el de newlib/vitasdk. El motor lee directamente st_mode en el offset
// 16 y st_size en el 48 (confirmado desensamblando MC_fsFileAttribute, el
// unico call site de stat en libzenonia2.so). Pasarle el struct stat de
// newlib dejaba esos offsets con basura de stack: al cargar una partida,
// MC_fsFileAttribute devolvia un "tamano" que era un puntero del heap
// (MALLOC FAILED FOR SIZE 0x81340CE0) y el motor crasheaba.
typedef struct {
    uint64_t st_dev;         // 0
    uint8_t  __pad0[4];      // 8
    uint32_t __st_ino;       // 12
    uint32_t st_mode;        // 16  <- leido por el motor
    uint32_t st_nlink;       // 20
    uint32_t st_uid;         // 24
    uint32_t st_gid;         // 28
    uint64_t st_rdev;        // 32
    uint8_t  __pad3[4];      // 40 (+4 de alineacion implicita)
    int64_t  st_size;        // 48  <- leido por el motor
    uint32_t st_blksize;     // 56 (+4 de alineacion implicita)
    uint64_t st_blocks;      // 64
    uint32_t st_atime;       // 72
    uint32_t st_atime_nsec;  // 76
    uint32_t st_mtime;       // 80
    uint32_t st_mtime_nsec;  // 84
    uint32_t st_ctime;       // 88
    uint32_t st_ctime_nsec;  // 92
    uint64_t st_ino;         // 96
} bionic_stat_t;             // 104 bytes (el motor reserva espacio de sobra)

_Static_assert(__builtin_offsetof(bionic_stat_t, st_mode) == 16, "bionic st_mode");
_Static_assert(__builtin_offsetof(bionic_stat_t, st_size) == 48, "bionic st_size");

int stat_hook(const char* path, void* statbuf) {
    char new_path[256];
    translate_path(path, new_path, sizeof(new_path));

    struct stat st;
    int res = stat(new_path, &st);
    game_log("[FakeJNI] stat_hook: %s -> %s = %d (size=%ld)\n", path, new_path, res, res == 0 ? (long) st.st_size : -1L);
    if (res == 0 && statbuf) {
        bionic_stat_t *bst = (bionic_stat_t *) statbuf;
        memset(bst, 0, sizeof(*bst));
        bst->st_mode = st.st_mode; // los bits S_IFDIR/permisos son POSIX, coinciden
        bst->st_nlink = st.st_nlink;
        bst->st_uid = st.st_uid;
        bst->st_gid = st.st_gid;
        bst->st_size = st.st_size;
        bst->st_blksize = st.st_blksize;
        bst->st_blocks = st.st_blocks;
        bst->st_atime = st.st_atime;
        bst->st_mtime = st.st_mtime;
        bst->st_ctime = st.st_ctime;
        bst->__st_ino = st.st_ino;
        bst->st_ino = st.st_ino;
    }
    return res;
}

int access_hook(const char* path, int amode) {
    char new_path[256];
    translate_path(path, new_path, sizeof(new_path));
    game_log("[FakeJNI] access_hook: %s -> %s\n", path, new_path);
    return access(new_path, amode);
}

so_default_dynlib default_dynlib[] = {
    // Android Logging
    { "__android_log_print", (uintptr_t)&__android_log_print },

    // C++ ABI / GCC
    { "__cxa_begin_cleanup", (uintptr_t)&__cxa_begin_cleanup },
    { "__cxa_call_unexpected", (uintptr_t)&__cxa_call_unexpected },
    { "__cxa_guard_acquire", (uintptr_t)&__cxa_guard_acquire },
    { "__cxa_guard_release", (uintptr_t)&__cxa_guard_release },
    { "__cxa_type_match", (uintptr_t)&__cxa_type_match },
    { "__gnu_Unwind_Find_exidx", (uintptr_t)&__gnu_Unwind_Find_exidx },
    { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
    { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard },

    // Libc - Memory
    { "malloc", (uintptr_t)&malloc_wrapper },
    { "free", (uintptr_t)&free_wrapper },
    { "calloc", (uintptr_t)&calloc_wrapper },
    { "realloc", (uintptr_t)&realloc_wrapper },
    { "memcpy", (uintptr_t)&memcpy },
    { "memset", (uintptr_t)&memset },
    { "memmove", (uintptr_t)&memmove },
    
    // Libc - File I/O
    { "fopen", (uintptr_t)&fopen_hook },

    // Libc - String
    { "strcpy", (uintptr_t)&strcpy },
    { "strncpy", (uintptr_t)&strncpy },
    { "strcmp", (uintptr_t)&strcmp },
    { "strncmp", (uintptr_t)&strncmp },
    { "strcat", (uintptr_t)&strcat },
    { "strncat", (uintptr_t)&strncat },
    { "strlen", (uintptr_t)&strlen },
    { "strstr", (uintptr_t)&strstr },
    { "strchr", (uintptr_t)&strchr },

    // Libc - I/O
    { "fopen", (uintptr_t)&fopen_hook },
    { "fread", (uintptr_t)&fread },
    { "fwrite", (uintptr_t)&fwrite },
    { "fclose", (uintptr_t)&fclose },
    { "printf", (uintptr_t)&printf },
    { "vprintf", (uintptr_t)&vprintf },
    { "vsprintf", (uintptr_t)&vsprintf },
    { "putchar", (uintptr_t)&putchar },

    // Libc - Filesystem
    { "access", (uintptr_t)&access_hook },
    { "stat", (uintptr_t)&stat_hook },
    { "unlink", (uintptr_t)&unlink },
    { "rename", (uintptr_t)&rename },

    // Libc - Time and Math
    { "time", (uintptr_t)&time },
    { "gmtime", (uintptr_t)&gmtime },
    { "localtime", (uintptr_t)&localtime },
    { "atoi", (uintptr_t)&atoi },
    { "abort", (uintptr_t)&abort },

    // OpenGL ES 1.1 (Mapped to vitaGL)
    { "glActiveTexture", (uintptr_t)&glActiveTexture },
    { "glBindTexture", (uintptr_t)&glBindTexture },
    { "glClear", (uintptr_t)&glClear },
    { "glClearColorx", (uintptr_t)&glClearColorx_wrapper },
    { "glColorPointer", (uintptr_t)&glColorPointer_wrapper },
    { "glDisable", (uintptr_t)&glDisable_wrapper },
    { "glDisableClientState", (uintptr_t)&glDisableClientState_wrapper },
    { "glDrawArrays", (uintptr_t)&glDrawArrays_wrapper },
    { "glEnable", (uintptr_t)&glEnable_wrapper },
    { "glEnableClientState", (uintptr_t)&glEnableClientState_wrapper },
    { "glGenTextures", (uintptr_t)&glGenTextures },
    { "glHint", (uintptr_t)&glHint },
    { "glLoadIdentity", (uintptr_t)&glLoadIdentity_wrapper },
    { "glMatrixMode", (uintptr_t)&glMatrixMode_wrapper },
    { "glNormalPointer", (uintptr_t)&glNormalPointer },
    { "glOrthof", (uintptr_t)&glOrthof_wrapper },
    { "glTexCoordPointer", (uintptr_t)&glTexCoordPointer_wrapper },
    { "glTexEnvf", (uintptr_t)&glTexEnvf_wrapper },
    { "glTexImage2D", (uintptr_t)&glTexImage2D_wrapper },
    { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D_wrapper },
    { "glTexParameterx", (uintptr_t)&glTexParameterx_wrapper },
    { "glVertexPointer", (uintptr_t)&glVertexPointer_wrapper },
    { "glViewport", (uintptr_t)&glViewport_wrapper },

    // Fixed point wrappers
    { "glClearColorx", (uintptr_t)&glClearColorx_wrapper },
    { "glTexParameterx", (uintptr_t)&glTexParameterx_wrapper },

    // Network Sockets
    { "socket", (uintptr_t)&socket },
    { "connect", (uintptr_t)&connect },
    { "send", (uintptr_t)&send },
    { "recv", (uintptr_t)&recv },
    { "shutdown", (uintptr_t)&shutdown },
    { "inet_addr", (uintptr_t)&inet_addr },

    // Core C library
    { "__errno", (uintptr_t)&__errno },
};

int default_dynlib_size = sizeof(default_dynlib);
