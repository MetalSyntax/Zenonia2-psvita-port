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

/**
 * @brief Resolves a game-relative asset name to an on-disk path.
 *
 * Tries `ux0:data/zenonia-2/<name>` first, then falls back to
 * `ux0:data/zenonia-2/assets/<name>`.
 *
 * @param name Asset-relative path as passed by the engine.
 * @param out Buffer to receive the resolved absolute path.
 * @param out_size Size of @p out.
 * @return 1 if a path was resolved (written to @p out), 0 otherwise.
 * @note Ver docs/loader/java.md#zenonia_resolve_asset_path-línea-24 para el razonamiento de diseño.
 */
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
    /**
     * @brief Alias of "readAssets" under its misspelled form.
     * @note Both spellings exist in libzenonia2.so; see
     *       docs/loader/java.md#nametomethodid--entrada-readassete-línea-49.
     */
    { 3, "readAssete", METHOD_TYPE_OBJECT },
    { 2, "isAssetExist", METHOD_TYPE_INT },
    { 4, "getGLOptionLinear", METHOD_TYPE_INT },
    { 5, "SetSpeed", METHOD_TYPE_VOID },
    { 6, "getPhoneModel", METHOD_TYPE_OBJECT },
    { 7, "getAbsolueFilePath", METHOD_TYPE_OBJECT },
    { 8, "OnUIStatusChange", METHOD_TYPE_VOID },
    { 9, "OnSoundPlay", METHOD_TYPE_VOID },
    /**
     * @brief Stop-all-audio callback; dispatches to audio.c.
     * @note "hideLoadingDialog"/"OnShowSaveButton" are Java-UI-only, no
     *       native equivalent. See
     *       docs/loader/java.md#nametomethodid--entrada-onstopsound-línea-62.
     */
    { 10, "OnStopSound", METHOD_TYPE_VOID },
    { 11, "hideLoadingDialog", METHOD_TYPE_VOID },
    { 12, "OnShowSaveButton", METHOD_TYPE_VOID },
    /**
     * @brief Vibration callback; no-op (Vita has no vibration engine).
     * @note Must stay registered so GetStaticMethodID caches a valid id
     *       instead of NULL, which would otherwise force a fresh lookup
     *       (and log spam) on every call. See
     *       docs/loader/java.md#nametomethodid--entrada-onvibrate-línea-67.
     */
    { 13, "OnVibrate", METHOD_TYPE_VOID },
    { 14, "getPhoneNumber", METHOD_TYPE_OBJECT },
    { 15, "TrackEventDispatch", METHOD_TYPE_VOID },
};

/**
 * @brief UI state last reported by the engine via OnUIStatusChange.
 *
 * 0 = logo, 1 = title (Java-UI screens, invisible here), >=2 = native
 * engine content (menu/game). Consumed by main.c to decide when to stop
 * drawing the splash overlay.
 * @note Ver docs/loader/java.md#g_ui_status-línea-81 para el razonamiento de diseño.
 */
volatile int g_ui_status = -1;

/**
 * @brief Reads an asset file and returns it as a raw Dalvik-shaped byte array.
 *
 * The returned block is NOT a FalsoJNI jbyteArray: it mimics Dalvik's
 * internal ArrayObject layout directly (16-byte header + raw element data)
 * because the engine reads it that way instead of via GetByteArrayElements.
 * @note Ver docs/loader/java.md#zenonia_readassets--layout-de-jbytearray-línea-87
 *       para el razonamiento de diseño.
 *
 * @param id Unused (dispatch-table method id).
 * @param args Varargs; expects a single jstring (asset-relative path).
 * @return Pointer to a Dalvik-shaped ArrayObject block, or NULL on failure.
 * @note Logging is capped at READASSETS_LOG_CAP happy-path calls; genuine
 *       error logs (open/size failures) are never capped. See
 *       docs/loader/java.md#readassets_log--límite-de-logging-línea-94.
 */
static int readassets_log = 0;
#define READASSETS_LOG_CAP 20

jobject Zenonia_readAssets(jmethodID id, va_list args) {
    jstring filename = va_arg(args, jstring);
    const char *name = (const char *) filename;
    if (readassets_log < READASSETS_LOG_CAP)
        game_log("[Java] readAssets: %s\n", name ? name : "(null)");

    if (!name) return NULL;

    char path[256];
    if (!zenonia_resolve_asset_path(name, path, sizeof(path))) {
        if (readassets_log < READASSETS_LOG_CAP)
            game_log("[Java] readAssets: not found (tried bare and assets/-prefixed): %s\n", name);
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        game_log("[Java] readAssets: failed to open %s\n", path);
        return NULL;
    }

    /**
     * @brief Get file size via fstat (not fseek+ftell).
     * @note Ver docs/loader/java.md#zenonia_readassets--fstat-en-vez-de-fseekftell-línea-126
     *       para el razonamiento de diseño.
     */
    struct stat st;
    long size = -1;
    if (fstat(fileno(f), &st) == 0) {
        size = st.st_size;
    }

    /**
     * @brief Peek and log the first 8 header bytes unconditionally.
     * @note Ver docs/loader/java.md#zenonia_readassets--log-de-tamaño-y-primeros-bytes-línea-137
     *       para el razonamiento de diseño.
     */
    unsigned char peek[8] = {0};
    long cur = ftell(f);
    fseek(f, 0, SEEK_SET);
    fread(peek, 1, size < 8 ? (size_t) size : 8, f);
    fseek(f, cur, SEEK_SET);
    if (readassets_log < READASSETS_LOG_CAP)
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

    if (readassets_log < READASSETS_LOG_CAP) {
        game_log("[Java] readAssets: Success. Size: %ld bytes\n", size);
        readassets_log++;
    }
    return array_obj;
}

/**
 * @brief Checks whether an asset exists and returns its size.
 *
 * Must be registered (not left as "not found") so a lookup miss on this
 * method ID can never be misread by the engine as a truthy result.
 * @note Ver docs/loader/java.md#zenonia_isassetexist--registro-obligatorio-por-bug-de-falso-positivo-línea-181
 *       para el razonamiento de diseño.
 *
 * @param id Unused (dispatch-table method id).
 * @param args Varargs; expects a single jstring (asset-relative path).
 * @return File size in bytes if it exists (and is not a directory), 0 otherwise.
 * @note Logging is capped at ISASSETEXIST_LOG_CAP calls. See
 *       docs/loader/java.md#isassetexist_log--límite-de-logging-línea-188.
 */
static int isassetexist_log = 0;
#define ISASSETEXIST_LOG_CAP 20

jint Zenonia_isAssetExist(jmethodID id, va_list args) {
    jstring filename = va_arg(args, jstring);
    const char *name = (const char *) filename;
    if (!name) return 0;

    char path[256];
    if (zenonia_resolve_asset_path(name, path, sizeof(path))) {
        struct stat st;
        if (stat(path, &st) == 0 && !S_ISDIR(st.st_mode)) {
            if (isassetexist_log < ISASSETEXIST_LOG_CAP) {
                game_log("[Java] isAssetExist: %s -> %ld (%s)\n", name, (long)st.st_size, path);
                isassetexist_log++;
            }
            return (jint)st.st_size;
        }
    }

    if (isassetexist_log < ISASSETEXIST_LOG_CAP) {
        game_log("[Java] isAssetExist: %s -> 0 (not found)\n", name);
        isassetexist_log++;
    }
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

/**
 * @brief Returns the base data path (engine's misspelled "getAbsolueFilePath").
 * @return A Dalvik JNI string (raw char pointer, per FalsoJNI's string
 *         representation) with a trailing slash, so the engine's own
 *         concatenation of the asset name yields a valid absolute path.
 * @note Ver docs/loader/java.md#zenonia_getabsoluefilepath-línea-236 para el razonamiento de diseño.
 */
jobject Zenonia_getAbsolueFilePath(jmethodID id, va_list args) {
    return (jobject) "ux0:data/zenonia-2/";
}

extern void update_virtual_buttons(int status);
extern void (*SetShowSaveButton)(void *env, void *obj, int show);

void Zenonia_OnUIStatusChange(jmethodID id, va_list args) {
    int status = va_arg(args, int);
    game_log("[Java] OnUIStatusChange: %d\n", status);
    g_ui_status = status;
    update_virtual_buttons(status);
}

void Zenonia_OnShowSaveButton(jmethodID id, va_list args) {
#ifndef HIDE_VIRTUAL_BUTTONS
    if (SetShowSaveButton)
        SetShowSaveButton(&jni, NULL, 1);
#endif
}

void Zenonia_VoidNoop(jmethodID id, va_list args) {
}

/**
 * @brief Dispatches a sound-play request to audio.c's mixer.
 *
 * Real signature (Natives.java): OnSoundPlay(int sndID, int vol, boolean isLoop).
 * @param id Unused (dispatch-table method id).
 * @param args Varargs: sndID (int), vol (int), isLoop (jboolean/int).
 * @note Ver docs/loader/java.md#zenonia_onsoundplay-línea-263 para el razonamiento de diseño.
 */
void Zenonia_OnSoundPlay(jmethodID id, va_list args) {
    int snd_id = va_arg(args, int);
    int vol = va_arg(args, int);
    int is_loop = va_arg(args, int); // jboolean
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
    { 14, Zenonia_getPhoneModel }, // getPhoneNumber: mismo no-op (NULL) que getPhoneModel
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { 5, Zenonia_SetSpeed },
    { 8, Zenonia_OnUIStatusChange },
    { 9, Zenonia_OnSoundPlay },
    { 10, Zenonia_OnStopSound },
    { 11, Zenonia_VoidNoop },
    { 12, Zenonia_OnShowSaveButton },
    { 13, Zenonia_VoidNoop }, // OnVibrate: Vita no tiene motor de vibracion
    { 15, Zenonia_VoidNoop }, // TrackEventDispatch: telemetria de Android, sin equivalente
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
