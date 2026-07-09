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

extern void game_log(const char *fmt, ...);

/*
 * JNI Methods
 */

NameToMethodID nameToMethodId[] = {
    { 1, "readAssets", METHOD_TYPE_OBJECT },
};

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
    snprintf(path, sizeof(path), "ux0:data/zenonia-2/%s", name);

    FILE *f = fopen(path, "rb");
    if (!f) {
        game_log("[Java] readAssets: failed to open %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *array_obj = malloc(16 + size);
    if (!array_obj) {
        fclose(f);
        return NULL;
    }

    memset(array_obj, 0, 16); // zero Dalvik ArrayObject header
    fread((char *) array_obj + 16, 1, size, f);
    fclose(f);

    return array_obj;
}

MethodsBoolean methodsBoolean[] = {};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};
MethodsInt methodsInt[] = {};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { 1, Zenonia_readAssets },
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {};

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
