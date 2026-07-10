#include <stdio.h>
#include <stdint.h>
#include <GLES/gl.h>

extern void game_log(const char *fmt, ...);

void dump_vertices(const void *pointer, int count) {
    int32_t *v = (int32_t *)pointer;
    game_log("  -> Vertices: ");
    for (int i=0; i<count; i++) {
        game_log("(%d, %d) ", v[i*2], v[i*2+1]);
    }
    game_log("\n");
}

void dump_uvs(const void *pointer, int count) {
    float *uv = (float *)pointer;
    game_log("  -> UVs: ");
    for (int i=0; i<count; i++) {
        game_log("(%.2f, %.2f) ", uv[i*2], uv[i*2+1]);
    }
    game_log("\n");
}
