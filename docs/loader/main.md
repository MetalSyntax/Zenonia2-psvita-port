# `loader/main.c` — Documentación de diseño

Comentarios explicativos extraídos del código fuente y reemplazados por bloques Doxygen técnicos en el código. Este documento conserva el razonamiento (el "por qué") separado de la documentación técnica.

## `kuKernelCpuUnrestrictedMemcpy` (extern, línea ~30)

**Archivo:** `loader/main.c` — **Declaración:** `extern int kuKernelCpuUnrestrictedMemcpy(...)`

> De kubridge.h (no se incluye entero: sus structs SceKernelAddrPair/etc.
> chocan con los del vitasdk actual; so_util.c ya lo enlaza igual)

---

## `gl_active` (línea ~42)

**Archivo:** `loader/main.c` — **Variable:** `gl_active`

> Once vitaGL owns the display, debugScreen's raw framebuffer writes must not
> keep running alongside it -- both would fight over the same framebuffer.

---

## `init_log()` (línea ~51)

**Archivo:** `loader/main.c` — **Función:** `init_log`

> One log file per run, named with its start timestamp, inside logs/ --
> keeps a full history across test runs instead of overwriting the same
> log.txt every time (see psvita-porting skill's hardware_debugging.md).

---

## `game_log()` (línea ~67)

**Archivo:** `loader/main.c` — **Función:** `game_log`

> Solo a archivo: la consola de debug en pantalla ya no se usa durante el
> arranque normal (el usuario ve el splash de bg0 en su lugar, ver splash_*).

---

## `fatal_error()` (línea ~90)

**Archivo:** `loader/main.c` — **Función:** `fatal_error`

> La pantalla de debug se inicializa recien aca: en un arranque sano no
> se muestra nunca ningun texto por pantalla.

---

## Protocolo de input / `queue_input_event()` (línea ~168)

**Archivo:** `loader/main.c` — **Función/sección:** `queue_input_event` y las constantes `MH_*`/`HAL_KEY_*`

> Input: replica del protocolo del APK (NexusGLRenderer + UIFullTouch +
> Zenonia2UIControllerView, confirmado por decompilacion con jadx). Cada
> evento se entrega DOS veces, igual que en Java: setInputEvent() inmediato
> al generarse, y handleCletEvent() justo antes del siguiente NativeRender
> (NexusGLRenderer.drawFrame -> sendHandleCletEvent). El touch va en el
> espacio interno del juego de 400x240 (UIFullTouch.convertScreenX/Y), no en
> pixeles de pantalla.

---

## `btn_map[]` (línea ~218)

**Archivo:** `loader/main.c` — **Variable:** `btn_map`

> Botones fisicos de la Vita -> teclas HAL que en el telefono generaba la UI
> tactil de Java (dpad y botones en pantalla), que aca no existe.

---

## `apply_so_patches()` — parche `CMvLayerData::PreLoad` (línea ~230)

**Archivo:** `loader/main.c` — **Función:** `apply_so_patches`

> Parches binarios al .so (aplicar despues de so_relocate/so_resolve y
> ANTES de so_flush_caches, que sincroniza la cache de instrucciones)
>
> CMvLayerData::PreLoad+0x20 (VA 0xaec38): `cmp r3, #0; ble <skip>` donde r3
> es el PUNTERO al buffer del mapa pasado como `long` (con signo). En Android
> el heap vive en direcciones bajas (positivas) y el chequeo pasa; en Vita
> nuestro heap newlib esta en 0x81xxxxxx, negativo como entero con signo, asi
> que el motor "cree" que el buffer es invalido, saltea el calloc de las capas
> del mapa y CMvMap::CreateMiniMap crashea despues leyendo la capa NULL
> (Data abort confirmado con vita-parse-core: PC=CreateMiniMap+0xaa, R1=0).
> Se cambia `ble` (0xdd27) por `beq` (0xd027): solo saltear si es NULL real.

---

## `apply_so_patches()` — ocultar D-pad/botones virtuales (línea ~257)

**Archivo:** `loader/main.c` — **Función:** `apply_so_patches` (bloque `#ifdef HIDE_VIRTUAL_BUTTONS`)

> Ocultar D-pad y botones virtuales en pantalla:
> Parchear drawDpad (VA 0x52980) y drawButton (VA 0x52a50) con `bx lr` (0x4770)
> para que retornen inmediatamente sin dibujar el overlay táctil de móvil.

---

## Splash de logo/título — `g_ui_status` (línea ~288)

**Archivo:** `loader/main.c` — **Variable externa:** `g_ui_status` / función `splash_load`

> Splash: logo.png/title.png/touch.png reales del APK (no el bg0 de
> LiveArea, que tiene el logo achicado y centrado sobre bordes negros
> pensados para la safe zone de LiveArea, no para pantalla completa) en
> pantalla hasta que el motor dibuje contenido real. Los estados 0 (logo
> Gamevil) y 1 (titulo) eran UI de Java en Android (aca se verian blancos);
> a partir del estado 2 el motor nativo ya dibuja el menu. En el estado 1,
> Android ademas parpadeaba touch.png ("toca para continuar", ver
> Zenonia2UIControllerView.showTouchViewAnim/TouchViewTimeTask en el APK
> decompilado) centrado horizontalmente a 3/4 de la pantalla -- sin ese
> aviso la pantalla de titulo se ve "trabada" hasta que el usuario prueba
> de tocar/apretar por su cuenta. g_ui_status lo actualiza java.c.

---

## `DRAWABLE_DIR` (línea ~301)

**Archivo:** `loader/main.c` — **Macro:** `DRAWABLE_DIR`

> logo.png/title.png/touch.png se leen tal cual vienen de
> apk_extract/res/drawable -- deployadas aparte por FTP (ver manage_vita.py)
> bajo ux0:data/zenonia-2/drawable/, no empaquetadas en el VPK -- y se
> decodifican/escalan en el dispositivo (ver image_load.c) en vez de leer
> los .rgba crudos pre-generados que este reemplaza.

---

## `TITLE_COVER_SCALE` / `touch_tex` (línea ~311)

**Archivo:** `loader/main.c` — **Macro:** `TITLE_COVER_SCALE`

> touch.png se escala por el mismo factor "cover" que title.png (800x480 ->
> 960x544, factor 1.2x) en vez de un cover-fit propio calculado de su
> aspecto, para que se vea consistente con el arte del titulo.

---

## `touch_draw()` — posición y parpadeo (línea ~361)

**Archivo:** `loader/main.c` — **Función:** `touch_draw`

> Aviso "toca para continuar" sobre el titulo (estado 1), replicando la
> posicion original (centrado, topMargin = 3/4 de pantalla) y el parpadeo
> de Zenonia2UIControllerView vía un pulso de alpha en vez del fade casi
> imperceptible original (0.0 a 0.1 de alpha), para que se note en pantalla.

---

## `touch_draw()` — vertex array `static` (línea ~389)

**Archivo:** `loader/main.c` — **Función:** `touch_draw`

> static: vitaGL no consume los vertex arrays de inmediato (los referencia
> para el command buffer de GXM), asi que un array en el stack local aca
> queda invalido para cuando efectivamente se dibuja -- eso generaba la
> franja diagonal de colores basura reportada tras agregar este quad.

---

## `touch_draw()` — restaurar `GL_TEXTURE_ENV_MODE` (línea ~408)

**Archivo:** `loader/main.c` — **Función:** `touch_draw`

> El motor deja GL_TEXTURE_ENV_MODE en GL_REPLACE una sola vez al iniciar
> y nunca lo vuelve a tocar por frame (confirmado en el log: solo aparece
> una vez al arrancar, nunca de nuevo en el bucle de dibujo del quad
> compuesto 400x240). Bajo GL_REPLACE el color array por-vertice que el
> motor deja armado (glColorPointer, no usado por REPLACE) es irrelevante;
> si acá se deja en GL_MODULATE, ese color por-vertice (con valores no
> pensados para modular nada) empieza a multiplicar la textura del motor
> en TODOS los frames siguientes -- eso era la franja diagonal roja/verde
> reportada en menu y juego, no un problema de los vertex arrays.

---

## `log_active_frame_buf()` (línea ~425)

**Archivo:** `loader/main.c` — **Función:** `log_active_frame_buf`

> Logs whatever sceDisplayGetFrameBuf currently reports as the buffer being
> scanned out, so we can tell from the log alone (without relying on what's
> visible on the TV/screen) whether vitaGL's swaps are actually taking over the
> display from debugScreen's own buffer.

---

## `gl_init()` — sin MSAA / sin triple buffering (línea ~440)

**Archivo:** `loader/main.c` — **Función:** `gl_init`

> No MSAA / no triple buffering: this is the config known to work on
> real hardware (see port_progress.md for the Vita3K investigation --
> vitaGL init reliably crashes inside Vita3K's own call_import dispatcher
> regardless of these settings, confirmed to be an emulator-session
> instability rather than a port bug, so untested against real hardware
> yet by this specific build).

---

## `gl_init()` — semántica del retorno de `vglInitExtended` (línea ~447)

**Archivo:** `loader/main.c` — **Función:** `gl_init`

> vglInitExtended's return value is GL_TRUE only if the requested resolution
> had to be reduced to fit the display's max (res_fallback in vitaGL's own
> source) -- it is NOT a success/failure code, so GL_FALSE here (960x544 is
> the Vita's native resolution, never falls back) is the expected, healthy
> result. Do not treat it as an init failure.

---

## `gl_init()` — cap de FPS con `eglSwapInterval` (línea ~454)

**Archivo:** `loader/main.c` — **Función:** `gl_init`

> Cap a ~30 FPS con VSync real (sin tearing): la Vita refresca a ~59.94Hz,
> asi que interval=2 espera 2 vblanks por swap en vez de 1 (que daria un
> cap a ~60 FPS via vglWaitVblankStart). Sin este cap, con el compositor
> por-software corriendo mas rapido que antes (boost de clocks Fase 15 +
> conversion RGB565 optimizada Fase 16.1), el motor llegaba a ~40 FPS
> sostenidos -- mas rapido que el ritmo original (30 FPS, hardware Android
> de 2011) para el que esta calibrada la logica de juego, y sin VSync
> (tearing visible en el blit del compositor).

---

## `main()` — boost de clocks (línea ~468)

**Archivo:** `loader/main.c` — **Función:** `main`

> Subir los clocks al maximo permitido por el firmware -- por defecto la
> Vita corre a 333MHz de CPU / 111MHz de bus / 166MHz de GPU. El motor
> hace bastante trabajo por-software (compositor 400x240, mixer de audio,
> parsing JNI), asi que este es el mismo boost estandar que usan
> practicamente todos los homebrews/ports (PPSSPP, etc.) y no tiene
> downside conocido en hardware real (solo mas consumo/calor).

---

## `main()` — bucle principal, estados de splash (línea ~712)

**Archivo:** `loader/main.c` — **Función:** `main` (bucle principal)

> Mientras el motor este en logo (0) / titulo (1) -- pantallas que
> eran UI de Java y aca se ven blancas -- tapar con el logo/titulo
> reales del APK. A partir del estado 2 (menu) el motor dibuja de verdad.

---
