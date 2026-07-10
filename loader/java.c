/*
 * java.c
 *
 * "Java-side" native method handlers that libzenonia2.so calls back into via
 * FalsoJNI (GetStaticMethodID("readAssets") + CallStaticObjectMethod(V)).
 * Everything else the engine might look up simply isn't registered here,
 * which FalsoJNI treats as "not found" (logged, non-fatal) rather than a
 * hardcoded 300-slot dummy vtable.
 */

#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "audio.h"

extern void game_log(const char *fmt, ...);

// readAssets/isAssetExist are always called with the same relative path (the
// engine calls isAssetExist(path) to decide whether to bother calling
// readAssets(path)), so both must resolve identically. The exact on-console
// layout wasn't confirmed yet when readAssets was first written -- try the
// bare path first (ux0:data/zenonia-2/<name>), then fall back to the
// assets/-prefixed one that dynlib.c's fopen_hook/stat_hook/access_hook use
// for everything else (ux0:data/zenonia-2/assets/<name>), and remember
// whichever one actually resolves so the two functions never disagree with
// each other for the same file.
static int zenonia_resolve_asset_path(const char *name, char *out, size_t out_size) {
    snprintf(out, out_size, "ux0:data/zenonia-2/%s", name);
    if (access(out, F_OK) == 0) return 1;

    snprintf(out, out_size, "ux0:data/zenonia-2/assets/%s", name);
    if (access(out, F_OK) == 0) return 1;

    return 0;
}

/*
 * JNI Methods
 */

NameToMethodID nameToMethodId[] = {
    { 1, "readAssets", METHOD_TYPE_OBJECT },
    // Real typo in libzenonia2.so itself (confirmed via `strings` -- both
    // "readAssets" and "readAssete" exist in the binary) and it's the one
    // actually looked up during boot per a real device log. Same handler,
    // just registered under both names since we don't know yet whether the
    // correctly-spelled one is also called later.
    { 3, "readAssete", METHOD_TYPE_OBJECT },
    { 2, "isAssetExist", METHOD_TYPE_INT },
    { 4, "getGLOptionLinear", METHOD_TYPE_INT },
    { 5, "SetSpeed", METHOD_TYPE_VOID },
    { 6, "getPhoneModel", METHOD_TYPE_OBJECT },
    { 7, "getAbsolueFilePath", METHOD_TYPE_OBJECT },
    { 8, "OnUIStatusChange", METHOD_TYPE_VOID },
    { 9, "OnSoundPlay", METHOD_TYPE_VOID },
    // OnStopSound corta todo el audio (audio.c); los otros dos son UI de Java
    // sin equivalente aca, no-ops para que no spameen "not found" en el log.
    { 10, "OnStopSound", METHOD_TYPE_VOID },
    { 11, "hideLoadingDialog", METHOD_TYPE_VOID },
    { 12, "OnShowSaveButton", METHOD_TYPE_VOID },
};

// Estado de UI que reporta el motor via OnUIStatusChange. main.c lo usa para
// saber cuando dejar de mostrar el splash: 0=logo y 1=titulo son pantallas
// que en Android dibujaba la UI de Java (invisibles aca); a partir de 2 el
// motor nativo ya dibuja contenido real (menu/juego).
volatile int g_ui_status = -1;

// The engine (built against an old pre-ART NDK) reads the jbyteArray this
// returns by reaching directly into Dalvik's internal ArrayObject layout
// (16-byte header, then raw element data) instead of going through
// GetByteArrayElements -- confirmed by the original hand-rolled loader code
// this replaces. FalsoJNI's own NewByteArray/JavaDynArray uses a different
// layout, so this can't go through it: it must keep returning a raw block
// shaped like Dalvik's ArrayObject.
jobject Zenonia_readAssets(jmethodID id, va_list args) {
    jstring filename = va_arg(args, jstring);
    const char *name = (const char *) filename;
    game_log("[Java] readAssets: %s\n", name ? name : "(null)");

    if (!name) return NULL;

    char path[256];
    if (!zenonia_resolve_asset_path(name, path, sizeof(path))) {
        game_log("[Java] readAssets: not found (tried bare and assets/-prefixed): %s\n", name);
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        game_log("[Java] readAssets: failed to open %s\n", path);
        return NULL;
    }

    // fstat instead of fseek(SEEK_END)+ftell: a bad size here (garbage,
    // sometimes literally bytes from the path string itself -- seen on a
    // real device as a "malloc FAILED FOR SIZE 1952539695" where that number
    // decoded to the ASCII text "/dat") was feeding a huge bogus length into
    // the engine's own allocator downstream (MC_knlCalloc), crashing it.
    struct stat st;
    long size = -1;
    if (fstat(fileno(f), &st) == 0) {
        size = st.st_size;
    }

    // Always log size + first bytes: several .zt1 assets are a custom
    // compressed format (4-byte compressed size, 4-byte uncompressed size,
    // then zlib data) that the engine reads directly, so a file that's the
    // wrong content (not corrupted size, just plain wrong bytes -- e.g. a
    // botched FTP transfer swapped in something else at this exact path)
    // won't be caught by the size check below but will still corrupt the
    // engine's own decompression step downstream. Logging the raw header
    // bytes here makes that visible without needing another crash dump.
    unsigned char peek[8] = {0};
    long cur = ftell(f);
    fseek(f, 0, SEEK_SET);
    fread(peek, 1, size < 8 ? (size_t) size : 8, f);
    fseek(f, cur, SEEK_SET);
    game_log("[Java] readAssets: %s size=%ld first8=%02x%02x%02x%02x%02x%02x%02x%02x\n",
        path, size, peek[0], peek[1], peek[2], peek[3], peek[4], peek[5], peek[6], peek[7]);

    if (size < 0 || size > 64 * 1024 * 1024) { // no single game asset should be anywhere near 64MB
        game_log("[Java] readAssets: bogus/oversized size %ld for %s, aborting\n", size, path);
        fclose(f);
        return NULL;
    }

    void *array_obj = malloc(16 + size);
    if (!array_obj) {
        fclose(f);
        return NULL;
    }

    memset(array_obj, 0, 16); // zero Dalvik ArrayObject header
    
    // Dalvik ArrayObject expects the length as a 32-bit integer at offset 8
    *(uint32_t *)((char *)array_obj + 8) = (uint32_t)size;

    fread((char *) array_obj + 16, 1, size, f);
    fclose(f);

    game_log("[Java] readAssets: Success. Size: %ld bytes\n", size);
    return array_obj;
}

// Registered because a not-found method ID makes FalsoJNI's methodIntCall()
// return -1 (see FalsoJNI_ImplBridge.c) -- a nonzero value the engine reads
// as a C-style boolean "true" (file exists), when it should be a clean 0.
// That false positive is what was crashing the engine: it went on to treat
// a nonexistent ptc/000.ptc as present and load it, faulting deep inside a
// kernel call downstream (confirmed via vita-parse-core on a real crash
// dump -- LR resolved to CMvResourceMgr::LoadAllPTCData()).
jint Zenonia_isAssetExist(jmethodID id, va_list args) {
    jstring filename = va_arg(args, jstring);
    const char *name = (const char *) filename;
    if (!name) return 0;

    char path[256];
    if (zenonia_resolve_asset_path(name, path, sizeof(path))) {
        struct stat st;
        if (stat(path, &st) == 0 && !S_ISDIR(st.st_mode)) {
            game_log("[Java] isAssetExist: %s -> %ld (%s)\n", name, (long)st.st_size, path);
            return (jint)st.st_size;
        }
    }
    
    game_log("[Java] isAssetExist: %s -> 0 (not found)\n", name);
    return 0;
}

jint Zenonia_getGLOptionLinear(jmethodID id, va_list args) {
    return 1; // 1 for linear filtering
}

void Zenonia_SetSpeed(jmethodID id, va_list args) {
    int speed = va_arg(args, int);
    game_log("[Java] SetSpeed: %d\n", speed);
}

jobject Zenonia_getPhoneModel(jmethodID id, va_list args) {
    return NULL;
}

jobject Zenonia_getAbsolueFilePath(jmethodID id, va_list args) {
    // Engine misspelled "Absolute" as "Absolue"
    // Returns a Dalvik JNI string, which FalsoJNI implements as a char pointer
    // We return the base path with a trailing slash so when the engine concatenates
    // the asset name, it forms a valid absolute path.
    return (jobject) "ux0:data/zenonia-2/";
}

void Zenonia_OnUIStatusChange(jmethodID id, va_list args) {
    int status = va_arg(args, int);
    game_log("[Java] OnUIStatusChange: %d\n", status);
    g_ui_status = status;
}

void Zenonia_VoidNoop(jmethodID id, va_list args) {
}

// Firma real (Natives.java): OnSoundPlay(int sndID, int vol, boolean isLoop)
// -- el segundo parametro es VOLUMEN, no loop (los logs viejos lo etiquetaban
// al reves). Despacha al mezclador de audio.c.
void Zenonia_OnSoundPlay(jmethodID id, va_list args) {
    int snd_id = va_arg(args, int);
    int vol = va_arg(args, int);
    int is_loop = va_arg(args, int); // jboolean
    game_log("[Java] OnSoundPlay: id=%d vol=%d isLoop=%d\n", snd_id, vol, is_loop);
    audio_play(snd_id, vol, is_loop);
}

void Zenonia_OnStopSound(jmethodID id, va_list args) {
    audio_stop_all();
}

MethodsBoolean methodsBoolean[] = {};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};
MethodsInt methodsInt[] = {
    { 2, Zenonia_isAssetExist },
    { 4, Zenonia_getGLOptionLinear },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { 1, Zenonia_readAssets },
    { 3, Zenonia_readAssets },
    { 6, Zenonia_getPhoneModel },
    { 7, Zenonia_getAbsolueFilePath },
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { 5, Zenonia_SetSpeed },
    { 8, Zenonia_OnUIStatusChange },
    { 9, Zenonia_OnSoundPlay },
    { 10, Zenonia_OnStopSound },
    { 11, Zenonia_VoidNoop },
    { 12, Zenonia_VoidNoop },
};

/*
 * JNI Fields
 */

NameToFieldID nameToFieldId[] = {};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {};
FieldsObject fieldsObject[] = {};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
