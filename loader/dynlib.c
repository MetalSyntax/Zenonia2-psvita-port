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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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

int stat_hook(const char* path, struct stat* statbuf) {
    char new_path[256];
    translate_path(path, new_path, sizeof(new_path));
    game_log("[FakeJNI] stat_hook: %s -> %s\n", path, new_path);
    return stat(new_path, statbuf);
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
    { "glColorPointer", (uintptr_t)&glColorPointer },
    { "glDisable", (uintptr_t)&glDisable },
    { "glDisableClientState", (uintptr_t)&glDisableClientState },
    { "glDrawArrays", (uintptr_t)&glDrawArrays },
    { "glEnable", (uintptr_t)&glEnable },
    { "glEnableClientState", (uintptr_t)&glEnableClientState },
    { "glGenTextures", (uintptr_t)&glGenTextures },
    { "glHint", (uintptr_t)&glHint },
    { "glLoadIdentity", (uintptr_t)&glLoadIdentity },
    { "glMatrixMode", (uintptr_t)&glMatrixMode },
    { "glNormalPointer", (uintptr_t)&glNormalPointer },
    { "glOrthof", (uintptr_t)&glOrthof },
    { "glTexCoordPointer", (uintptr_t)&glTexCoordPointer },
    { "glTexEnvf", (uintptr_t)&glTexEnvf },
    { "glTexImage2D", (uintptr_t)&glTexImage2D },
    { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D },
    { "glVertexPointer", (uintptr_t)&glVertexPointer },
    { "glViewport", (uintptr_t)&glViewport },

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
