# `loader/java.c` — Documentación de diseño

Comentarios explicativos extraídos del código fuente y reemplazados por bloques Doxygen técnicos en el código. Este documento conserva el razonamiento (el "por qué") separado de la documentación técnica.

## `zenonia_resolve_asset_path` (línea ~24)

**Archivo:** `loader/java.c` — **Función:** `zenonia_resolve_asset_path`

> readAssets/isAssetExist are always called with the same relative path (the
> engine calls isAssetExist(path) to decide whether to bother calling
> readAssets(path)), so both must resolve identically. The exact on-console
> layout wasn't confirmed yet when readAssets was first written -- try the
> bare path first (ux0:data/zenonia-2/<name>), then fall back to the
> assets/-prefixed one that dynlib.c's fopen_hook/stat_hook/access_hook use
> for everything else (ux0:data/zenonia-2/assets/<name>), and remember
> whichever one actually resolves so the two functions never disagree with
> each other for the same file.

---

## `nameToMethodId` — entrada `"readAssete"` (línea ~49)

**Archivo:** `loader/java.c` — **Estructura:** `nameToMethodId[]`, id 3 (`"readAssete"`)

> Real typo in libzenonia2.so itself (confirmed via `strings` -- both
> "readAssets" and "readAssete" exist in the binary) and it's the one
> actually looked up during boot per a real device log. Same handler,
> just registered under both names since we don't know yet whether the
> correctly-spelled one is also called later.

---

## `nameToMethodId` — entrada `"OnStopSound"` (línea ~62)

**Archivo:** `loader/java.c` — **Estructura:** `nameToMethodId[]`, ids 10-12 (`"OnStopSound"`, `"hideLoadingDialog"`, `"OnShowSaveButton"`)

> OnStopSound corta todo el audio (audio.c); los otros dos son UI de Java
> sin equivalente aca, no-ops para que no spameen "not found" en el log.

---

## `nameToMethodId` — entrada `"OnVibrate"` (línea ~67)

**Archivo:** `loader/java.c` — **Estructura:** `nameToMethodId[]`, id 13 (`"OnVibrate"`)

> Vita no tiene motor de vibracion -- no-ops legitimos. Registrados
> ademas porque un metodo sin registrar hace que GetStaticMethodID
> devuelva NULL, y el motor no cachea ese NULL: vuelve a intentar el
> lookup en cada llamada (p.ej. cada golpe en combate intenta vibrar),
> spameando "[JNI ERR] ... not found" con su fflush a disco por llamada
> -- visto en logs/log_1784331019.txt (43 veces solo en esa sesion,
> concentradas durante gameplay real). Registrar el metodo (aunque sea
> no-op) hace que el motor obtenga un id valido y lo cachee, cortando el
> spam de raiz.

---

## `g_ui_status` (línea ~81)

**Archivo:** `loader/java.c` — **Variable:** `g_ui_status`

> Estado de UI que reporta el motor via OnUIStatusChange. main.c lo usa para
> saber cuando dejar de mostrar el splash: 0=logo y 1=titulo son pantallas
> que en Android dibujaba la UI de Java (invisibles aca); a partir de 2 el
> motor nativo ya dibuja contenido real (menu/juego).

---

## `Zenonia_readAssets` — layout de `jbyteArray` (línea ~87)

**Archivo:** `loader/java.c` — **Función:** `Zenonia_readAssets`

> The engine (built against an old pre-ART NDK) reads the jbyteArray this
> returns by reaching directly into Dalvik's internal ArrayObject layout
> (16-byte header, then raw element data) instead of going through
> GetByteArrayElements -- confirmed by the original hand-rolled loader code
> this replaces. FalsoJNI's own NewByteArray/JavaDynArray uses a different
> layout, so this can't go through it: it must keep returning a raw block
> shaped like Dalvik's ArrayObject.

---

## `readassets_log` — límite de logging (línea ~94)

**Archivo:** `loader/java.c` — **Variable:** `readassets_log` / **Función:** `Zenonia_readAssets`

> readAssets/isAssetExist se llaman muy seguido durante gameplay real (no
> solo en el boot) -- loguear cada llamada aca hacia un fflush() a disco por
> llamada en el thread de render (visto en logs/log_1784331019.txt: 445+477
> lineas de estas dos funciones sobre 1355 totales de esa sesion, muchas
> durante juego activo). Los logs de la ruta feliz (entrada, tamaño, éxito)
> se cortan a los primeros N para debug de arranque; los de error genuino
> (archivo no abre, tamaño corrupto) quedan sin cortar porque son señal de un
> bug real y no deberían repetirse en operación normal.

---

## `Zenonia_readAssets` — `fstat` en vez de `fseek`+`ftell` (línea ~126)

**Archivo:** `loader/java.c` — **Función:** `Zenonia_readAssets`

> fstat instead of fseek(SEEK_END)+ftell: a bad size here (garbage,
> sometimes literally bytes from the path string itself -- seen on a
> real device as a "malloc FAILED FOR SIZE 1952539695" where that number
> decoded to the ASCII text "/dat") was feeding a huge bogus length into
> the engine's own allocator downstream (MC_knlCalloc), crashing it.

---

## `Zenonia_readAssets` — log de tamaño y primeros bytes (línea ~137)

**Archivo:** `loader/java.c` — **Función:** `Zenonia_readAssets`

> Always log size + first bytes: several .zt1 assets are a custom
> compressed format (4-byte compressed size, 4-byte uncompressed size,
> then zlib data) that the engine reads directly, so a file that's the
> wrong content (not corrupted size, just plain wrong bytes -- e.g. a
> botched FTP transfer swapped in something else at this exact path)
> won't be caught by the size check below but will still corrupt the
> engine's own decompression step downstream. Logging the raw header
> bytes here makes that visible without needing another crash dump.

---

## `Zenonia_isAssetExist` — registro obligatorio por bug de falso positivo (línea ~181)

**Archivo:** `loader/java.c` — **Función:** `Zenonia_isAssetExist` (registro en `nameToMethodId[]`, id 2)

> Registered because a not-found method ID makes FalsoJNI's methodIntCall()
> return -1 (see FalsoJNI_ImplBridge.c) -- a nonzero value the engine reads
> as a C-style boolean "true" (file exists), when it should be a clean 0.
> That false positive is what was crashing the engine: it went on to treat
> a nonexistent ptc/000.ptc as present and load it, faulting deep inside a
> kernel call downstream (confirmed via vita-parse-core on a real crash
> dump -- LR resolved to CMvResourceMgr::LoadAllPTCData()).

---

## `isassetexist_log` — límite de logging (línea ~188)

**Archivo:** `loader/java.c` — **Variable:** `isassetexist_log` / **Función:** `Zenonia_isAssetExist`

> El motor llama isAssetExist repetidamente durante gameplay real, no solo en
> carga -- confirmado en logs/log_1784331019.txt: 227 misses solo de
> "sound/*.mmf" (formato de vibracion de Android que este puerto nunca provee,
> asi que siempre da "not found", una y otra vez sin cachear el resultado del
> lado del motor). Cada llamada antes hacia un fflush() a disco sin condicion,
> en el thread de render, sea que el asset exista o no. Cortado a los
> primeros N para debug de arranque.

---

## `Zenonia_getAbsolueFilePath` (línea ~236)

**Archivo:** `loader/java.c` — **Función:** `Zenonia_getAbsolueFilePath`

> Engine misspelled "Absolute" as "Absolue"
> Returns a Dalvik JNI string, which FalsoJNI implements as a char pointer
> We return the base path with a trailing slash so when the engine concatenates
> the asset name, it forms a valid absolute path.

---

## `Zenonia_OnSoundPlay` (línea ~263)

**Archivo:** `loader/java.c` — **Función:** `Zenonia_OnSoundPlay`

> Firma real (Natives.java): OnSoundPlay(int sndID, int vol, boolean isLoop)
> -- el segundo parametro es VOLUMEN, no loop (los logs viejos lo etiquetaban
> al reves). Despacha al mezclador de audio.c.

---
