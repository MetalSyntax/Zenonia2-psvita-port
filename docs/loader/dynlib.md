# `loader/dynlib.c` — Documentación de diseño

Comentarios explicativos extraídos del código fuente y reemplazados por bloques Doxygen técnicos en el código. Este documento conserva el razonamiento (el "por qué") separado de la documentación técnica.

## `rgba_conv_buf` (línea ~35)

**Archivo:** `loader/dynlib.c` — **Función/estructura:** `rgba_conv_buf` (usado por `convert_rgb565_to_rgba8888`)

> Buffer reusado entre llamadas -- esta conversion corre una vez por frame
> (el blit del compositor 400x240, ver glTexSubImage2D_wrapper) y antes hacia
> malloc()+free() de ~384KB en cada una, agregando churn de heap justo en el
> hot path de render. Nunca se libera; crece con realloc solo si hace falta
> mas espacio (una textura RGB565 mas grande que la vista hasta ahora).

---

## `convert_rgb565_to_rgba8888` — expansión NEON (línea ~56)

**Archivo:** `loader/dynlib.c` — **Función/estructura:** `convert_rgb565_to_rgba8888`

> Expansion 5/6-bit -> 8-bit vectorizada con NEON, 8 pixeles por
> iteracion. Formula multiply-add-shift (bit-replication) en vez de la
> division escalar original: mismo resultado +/-1 LSB (imperceptible),
> sin instruccion de division y vectorizable en registros de 16 bits sin
> overflow (31*527+23=16360 y 63*259+33=16350, ambos caben en 16 bits).

---

## `glTexImage2D_wrapper` — tamaño de fuente para post-proceso (línea ~115)

**Archivo:** `loader/dynlib.c` — **Función/estructura:** `glTexImage2D_wrapper`

> Unica textura RGB565 del motor: el buffer compuesto 400x240 (subido
> a un POT via glTexSubImage2D despues). El shader de post-proceso
> opcional necesita el tamaño real de este POT para su uniform de
> texel size -- ver postprocess.c.

---

## `glTexImage2D_wrapper` — experimento RGB565 nativo (línea ~121)

**Archivo:** `loader/dynlib.c` — **Función/estructura:** `glTexImage2D_wrapper` (rama `#ifdef NATIVE_RGB565_TEST`)

> Experimento Fase 18: subir RGB565 nativo, sin convertir. Si vitaGL
> sigue sin soportarlo, el chequeo glGetError() de mas abajo va a
> loguear GL_INVALID_ENUM (0x500) -- exactamente la señal que
> confirma/descarta esto sin ambiguedad.

---

## `glTexSubImage2D_wrapper` — firma del blit del compositor (línea ~160)

**Archivo:** `loader/dynlib.c` — **Función/estructura:** `glTexSubImage2D_wrapper`

> w=400 h=240 es la firma unica del blit del compositor (confirmado
> en log) -- marcarlo para que el proximo glDrawArrays use el shader
> de post-proceso opcional en vez de fixed-function GL_REPLACE.

---

## Conversión diferida de vértices `GL_FIXED` (línea ~183)

**Archivo:** `loader/dynlib.c` — **Función/estructura:** `pending_fixed_verts` / `glVertexPointer_wrapper` / `glDrawArrays_wrapper`

> vitaGL does not correctly consume GL_FIXED vertex arrays (same class of bug already
> worked around for glClearColorx_wrapper/glTexParameterx_wrapper above). This engine is a
> J2ME-derived port that feeds Q16.16 fixed-point vertex data, so passing GL_FIXED straight
> through leaves vitaGL reading the raw ints as if they were floats -> geometry collapses
> off-frustum and the screen stays blank even though the engine keeps rendering frames.
> glVertexPointer_wrapper defers conversion to glDrawArrays_wrapper below, since only there
> do we know how many vertices actually need converting.
> Defers GL_FIXED attribute conversions to glDrawArrays_wrapper since only there we know the count

---

## `glDrawArrays_wrapper` — desvío al post-proceso (línea ~270)

**Archivo:** `loader/dynlib.c` — **Función/estructura:** `glDrawArrays_wrapper`

> No-op (devuelve 0) a menos que se haya compilado con
> ENABLE_POSTPROCESS_SHADER Y este sea el blit del compositor marcado por
> glTexSubImage2D_wrapper -- en ese caso dibuja ella misma el quad (con
> su propia geometria, ver postprocess.c) y hay que saltearse el
> glDrawArrays original de mas abajo.

---

## `bionic_stat_t` — layout ABI de Android (línea ~497)

**Archivo:** `loader/dynlib.c` — **Función/estructura:** `bionic_stat_t` (usado por `stat_hook`)

> struct stat con el layout de bionic (Android ARM 32-bit, NDK android-9) --
> NO es el de newlib/vitasdk. El motor lee directamente st_mode en el offset
> 16 y st_size en el 48 (confirmado desensamblando MC_fsFileAttribute, el
> unico call site de stat en libzenonia2.so). Pasarle el struct stat de
> newlib dejaba esos offsets con basura de stack: al cargar una partida,
> MC_fsFileAttribute devolvia un "tamano" que era un puntero del heap
> (MALLOC FAILED FOR SIZE 0x81340CE0) y el motor crasheaba.

---
