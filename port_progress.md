# Registro de Progreso del Port de Zenonia 2 (PS Vita)

## Fase 1: Configuración del Entorno y Preparación (Completada)
- **Repositorio de Git:** Inicializado.
- **Exclusiones (.gitignore):** Se han ocultado los assets, binarios, archivos originales .apk, y directorios de compilación para evitar subidas accidentales a repositorios públicos.
- **Dependencias (CMakeLists.txt):** Se ha modificado el archivo `CMakeLists.txt` para incluir y enlazar los stubs necesarios de las bibliotecas de PS Vita: `vitaGL`, `mathneon`, `kubridge_stub`, `taihen_stub`, `SceDisplay_stub`, `SceCtrl_stub`, `SceTouch_stub`, `SceAudio_stub`, entre otros.
- **Código Base:** Se verificó la existencia del `loader/main.c` base.
- **Estado de VitaSDK:** Se ha detectado que `VITASDK` no está configurado actualmente en las variables de entorno de esta terminal, lo cual será necesario resolver antes del momento de compilación final (`make`).

## Fase 2: Análisis del Ejecutable Original (.so) (En Progreso)
- [x] **Archivo detectado:** `lib/libzenonia2.so` (1.4 MB).
- [x] **Análisis de Símbolos Exportados (JNI):** Hemos utilizado `nm` para inspeccionar la librería y hemos encontrado las funciones críticas de JNI que requerirán el Falso JNI (FakeJNI):
  - `JNI_OnLoad`
  - `NativeInit`
  - `NativeRender`
  - `NativeResize`
  - `NativePauseClet`
  - `NativeDestroyClet`
  - `NativeIsNexusOne`
- [x] Próximo paso: Analizar los símbolos importados (OpenGL ES, log, libc) en `imports.txt` para preparar la tabla de relocalización de SoLoader.
- [x] **Análisis de Símbolos Importados (Dependencias):** Tras revisar `imports.txt`, hemos identificado las dependencias exactas:
  - **Android Logging:** `__android_log_print` (Debe ser redirigido a `psvDebugScreenPrintf` o un log de archivo).
  - **Gráficos (OpenGL ES 1.1):** Utiliza funciones de pipeline fijo de GLES 1 como `glMatrixMode`, `glVertexPointer`, `glDrawArrays`, `glTexImage2D` y las variantes `x` de punto fijo como `glClearColorx` y `glTexParameterx`. Estas se enlazarán a `vitaGL`.
  - **Red / Sockets:** Funciones POSIX de red (`socket`, `connect`, `send`, `recv`, `shutdown`), útiles si el juego tiene tablas de puntuación o modo online.
  - **Archivos y Memoria (libc):** Llamadas estándar como `fopen`, `fread`, `malloc`, `free`, `stat`, `access`. Redirigiremos los accesos de archivo a `ux0:data/zenonia-2/assets/`.
  - **Resolución de símbolos adicionales:** El motor importa símbolos para la inicialización y limpieza de C++ nativo (`__cxa_begin_cleanup`, `__cxa_guard_acquire`, etc.), los cuales deberán ser enlazados directamente o usar stubs provistos por el toolchain de Vita.

## Fase 2 Completada.

---

## Fase 3: Carga Dinámica (SoLoader) (En Progreso)
- [x] **Desempaquetar Toolkit y SoLoader:** Hemos descargado los archivos de `so_util.c` y `so_util.h` (SoLoader) para el kernel de Vita y los hemos añadido a `CMakeLists.txt` y a la carpeta `loader/`.
- [x] **Modificar `main.c`:** Importadas las cabeceras de SoLoader y programada la llamada a `so_file_load` indicando la ruta `ux0:data/zenonia-2/libzenonia2.so` en la dirección base `0x98000000`.
- [x] **Mapeo Inicial de Memoria (Relocalización):** Preparada la estructura en memoria (`so_relocate`, `so_resolve`) y forzada la resolución de `__android_log_print` hacia la pantalla de depuración (`psvDebugScreenPrintf`) para atrapar los logs del motor.
- [x] **Símbolos restantes:** Creado el archivo `dynlib.c` y poblado el array `default_dynlib` con todas las dependencias vitales de libc (malloc, free, time, math, sockets) y las variantes de GLES 1 (como un wrapper para `glClearColorx` apuntando a las funciones estándar de `vitaGL`).

## Fase 4: Implementación de Falso JNI (FakeJNI) (En Progreso)
- [x] **Enlazado de Símbolos:** Se configuraron punteros a funciones en `main.c` para extraer dinámicamente `JNI_OnLoad`, `NativeInit`, `NativeRender` y `NativeResize` usando la macro `so_symbol`.
- [x] **Secuencia de Arranque (Android Lifecycle):** Se crearon punteros ficticios para el Entorno Java (`fake_vm` y `fake_env`) y se enviaron como parámetros para sortear los controles del juego y ejecutar el constructor gráfico `NativeInit`.
- [x] **Traducción JNI Crítica:** Se implementaron los métodos `GetStaticMethodID`, `CallStaticObjectMethodV`, y `NewStringUTF` para interceptar la lectura de assets (`readAssets`). Ahora el motor recibe directamente punteros de memoria locales con los binarios del juego cargados por el propio loader.
- [x] **vitaGL & Main Loop:** Se inicializó el contexto de OpenGL (`vglInitExtended`) con la resolución nativa de Vita (960x544) y se construyó el bucle infinito (`while(1)`) que ejecuta `NativeRender` por cada fotograma e intercambia los buffers (`vglSwapBuffers`). Se añadió una salida de emergencia presionando el botón `START`.

## Fase 5: Mapeo de Controles y Entrada Táctil (Completada)
- [x] **Detección de Exportaciones de Entrada:** Al inspeccionar los símbolos, se encontró `Java_com_gamevil_nexus2_Natives_setInputEvent`, la función que el framework de Gamevil usa para inyectar comandos de Android (Touches/Keys).
- [x] **Lectura Táctil de Vita:** Se habilitó el muestreo de la pantalla táctil capacitiva con `sceTouchSetSamplingState` y la lectura por frame en `sceTouchPeek`.
- [x] **Normalización y Conversión:** Las coordenadas en bruto del hardware táctil de Vita (1920x1088) se normalizan a la resolución del renderizador (960x544). Además, se programó un sistema básico de estado ("Touch Down", "Touch Move", "Touch Up") que inyecta el tipo de evento correspondiente hacia la máquina falsa de Java.

## Fase 6: Extraer Assets y Empaquetar a VPK (Completada)
- [x] **Migración de Assets:** Se ha extraído exitosamente el archivo `.apk` y transferido la carpeta `assets/` hacia el directorio de montaje para Vita `ux0_data/zenonia-2/assets/`.
- [x] **Empaquetado de LiveArea (sce_sys):** Se construyeron `icon0.png`, `pic0.png`, `startup.png` y el `template.xml` básico.

## Fase 7: Sistema de Archivos (File I/O) y Depuración de Crasheos (En Progreso)
- [x] **Intercepción de Archivos:** El motor original busca archivos relativos que PS Vita asume que están en `app0:/` de forma automática. Se escribieron *hooks* (`fopen_hook`, `stat_hook`, `access_hook`) inyectados dinámicamente mediante la tabla de dependencias (`default_dynlib`) para redirigir toda lectura hacia `ux0:data/zenonia-2/assets/`.
- [x] **Depuración de Crash (`EXC_BAD_ACCESS`):** Gracias al sistema de logs personalizado (`game_log`), descubrimos que las funciones internas del binario original usan agresivamente la llamada al sistema POSIX `stat` y `access` de forma relativa (por ejemplo `data/eng/XlsParticle.zt1`). La intercepción de estas llamadas soluciona crasheos por punteros inválidos al intentar leer archivos que no existían.

## Fase 8: Automatización e Integración Continua (Completada)
- [x] **Build Script Automatizado (`build.sh`):** Ejecuta de forma segura `cmake` y `make` en el directorio temporal `/tmp/zenonia2-build` y genera directamente los instaladores VPK listos para emulador y consola.
- [x] **Git:** Configuración inicial e ignorado correcto de carpetas de assets y binarios (`.gitignore`). Commit inicial.

## Siguiente Paso del Plan de Porteo Original (histórico, ver Fase 9 para el estado real)
Basado en `plan_zenonia_port.md`, **hemos completado las bases de File I/O y JNI Hooks.** El siguiente paso será realizar la prueba manual en Vita3K para comprobar que el *splash screen* del motor Gamevil o el menú principal se dibujan en pantalla sin interrupciones. Queda pendiente la **Fase: Audio (Música y SFX)** para traducir las llamadas de MediaPlayer originales hacia `SceAudio`.

**Nota importante (2026-07-08):** las Fases 4 y 5 arriba se marcaron como completadas, pero **nunca se probaron de punta a punta** — `vglInit(0)` estaba comentado ("TEMPORARILY DISABLED to avoid Vita3K SceShaccCg crash") y el FakeJNI hecho a mano solo cubría un puñado de las ~230 funciones de JNI. Ver Fase 9 para el trabajo real de puesta en marcha, qué se rompió al intentar probarlo por primera vez, y qué se arregló.

---

## Fase 9: Puesta en Marcha Real y Diagnóstico en Vita3K (2026-07-08)

Hasta este punto el port nunca se había ejecutado de punta a punta — Fases 4-6 quedaron marcadas `[x]` a partir del código escrito, no de una prueba real. Esta sesión fue la primera vez que se intentó arrancarlo en Vita3K, y se encontraron y corrigieron **dos bugs reales que impedían que el juego arrancara**, más un **tercer problema (crash de vitaGL en Vita3K) que resultó ser del propio emulador, no del port**.

### 9.1 — Bug real #1: el FakeJNI hecho a mano estaba incompleto y corrompía memoria

**Diagnóstico:** el log de la primera corrida (con `vglInit` aún deshabilitado) mostró que el motor llegaba lejos —`isAssetExist` para decenas de archivos— pero terminaba pidiendo un archivo con un prefijo de **basura binaria** en la ruta:
```
assets/<bytes-basura>/data/eng/XlsParticle.zt1
```
seguido de un crash real (`Invalid read of uint32_t at address: 0x8`, `ldr r2, [r7, #8]` con `r7=0`).

**Causa raíz:** la tabla `fake_env_vtable[300]` en `loader/jni_stubs.c` solo implementaba `GetStaticMethodID`, `CallStaticObjectMethod(V)` y `NewStringUTF` — las ~226 funciones restantes de `JNINativeInterface` eran un dummy que loguea y devuelve `0`/`NULL` sin excepción. Como `NewStringUTF` devolvía el `char*` real (truco válido y usado también por FalsoJNI), pero **`GetStringUTFChars` era uno de esos dummies** (siempre `NULL`, sin mirar su argumento), cualquier código del motor que hiciera `NewStringUTF(...)` seguido de `GetStringUTFChars(...)` sobre esa misma cadena recibía basura en vez del string real — de ahí el prefijo corrupto en la ruta. El mismo patrón de "media implementación de JNI" está documentado como riesgo conocido en la skill `psvita-porting` (`references/jni_stubs.md`).

**Solución:** se reemplazó por completo el FakeJNI hecho a mano por **[FalsoJNI](https://github.com/vitasdk/FalsoJNI)**, la misma librería (MIT, ~230 funciones de `JNINativeInterface`/`JNIInvokeInterface` correctamente implementadas) que usa el port de referencia *Prince of Persia Classic* (`../Prince of Persia/lib/falso_jni/`), donde sí está confirmado funcionando en hardware real y en Vita3K.

- `lib/falso_jni/` — vendorizado desde el proyecto de Prince of Persia (`FalsoJNI.c/h`, `FalsoJNI_Impl.h`, `FalsoJNI_ImplBridge.c/h`, `jni.h`, `LICENSE`). Se escribió un `FalsoJNI_Logger.c` propio (en vez de copiar el suyo) que enruta al `game_log()` de este proyecto en lugar de a su `utils/logger.h` (que Zenonia no tiene).
- `loader/jni_stubs.c` (borrado) → `loader/java.c` (nuevo): registra **solo** el método real que el motor necesita — `readAssets` — vía la tabla `nameToMethodId[]`/`methodsObject[]` de FalsoJNI. La implementación de `readAssets` se conservó tal cual estaba (incluyendo el truco de layout de `ArrayObject` de Dalvik con header de 16 bytes que el motor espera, en vez del `jbyteArray` abstracto de FalsoJNI) porque es una necesidad específica de este motor (compilado contra un NDK viejo pre-ART que accede al array devuelto directamente por puntero).
- `loader/main.c`: se quitaron `fake_vm_vtable`/`fake_env_vtable`/`dummy_jni_func` y se llamó a `jni_init()` de FalsoJNI; todas las llamadas a `JNI_OnLoad`/`NativeInit`/`NativeResize`/`NativeRender`/`setInputEvent` ahora reciben `&jvm`/`&jni` reales.
- `.gitignore`: se cambió `lib/` (que ocultaba también el FalsoJNI vendorizado) por `lib/libzenonia2.so` específicamente, ya que solo el `.so` del juego es material propietario.

### 9.2 — Bug real #2: vitaGL nunca se había inicializado

`// vglInit(0); // TEMPORARILY DISABLED to avoid Vita3K SceShaccCg crash` — el motor corría sin ningún contexto GL real desde el día 1. Se implementó `gl_init()` con `vglInitExtended(0, 960, 544, 6*1024*1024, SCE_GXM_MULTISAMPLE_NONE)` + `vglUseTripleBuffering(GL_FALSE)`, igual que la configuración confirmada funcionando en el port de Prince of Persia. Se movió antes de `NativeInit()` (el motor necesita un contexto GL válido para inicializar sus recursos gráficos).

### 9.3 — Ajustes de build necesarios para lo anterior

- **Colisión de nombres:** el puntero local `JNI_OnLoad` chocaba con el símbolo `JNI_OnLoad(JavaVM*, void*)` que declara `jni.h` de FalsoJNI → renombrado a `Game_JNI_OnLoad`.
- **pthread:** FalsoJNI usa `pthread_mutex_t` para su asignador de arrays dinámicos → se agregó `-Wl,--whole-archive pthread -Wl,--no-whole-archive` a `target_link_libraries`.
- **Conflicto de símbolos:** forzar `pthread` con `--whole-archive` arrastra objetos de `libc.a` que duplican símbolos ya provistos por `SceLibc_stub` (`fclose`, `fflush`) → se quitó `SceLibc_stub` de `target_link_libraries` (el proyecto no usa ninguna API específica de SceLibc, solo lo tenía enlazado sin necesidad).
- **`ATTRIBUTE2=12`** agregado a `VITA_MKSFOEX_FLAGS` (presupuesto de memoria extendido) — copiado del `param.sfo` de Prince of Persia. No demostró tener efecto sobre el crash de la sección 9.4, pero es una buena práctica igual mantenerlo para juegos con uso intensivo de GPU.
- **`UNSAFE NOASLR`** agregado a `vita_create_self` — probado como posible causa del crash de 9.4 (no lo fue), pero es una bandera estándar en homebrews de este tipo y no hace daño dejarla.
- Nueva opción de CMake `ENABLE_VERBOSE_JNI_LOG` (define `FALSOJNI_DEBUGLEVEL=0`) — **ON por defecto ahora, apagar antes de una build final**: loguea cada llamada JNI, útil para depurar en consola real pero cuesta rendimiento si se deja prendido (ver el problema exacto que esto causó en el Fixes_Log de Prince of Persia, puntos #12-13 — cada línea de log hace `sceIoOpen`+`sceIoWrite`+`sceIoClose`).

### 9.4 — Investigación profunda de un crash de vitaGL en Vita3K (bug del emulador, NO del port)

Al habilitar `gl_init()`, Vita3K crasheaba de forma **100% determinística** (misma dirección exacta, `Unhandled EXC_BAD_ACCESS at pc 0x18f6e3504`, siempre dentro de `call_import(EmuEnvState&, CPUState&, unsigned int, int)` de Vita3K mismo, en el hilo de la app) justo al cargar/iniciar `ur0:/data/libshacccg.suprx` (el compilador de shaders Cg que vitaGL usa internamente para su pipeline de función fija — necesario incluso si el juego solo usa llamadas GLES1 clásicas como `glVertexPointer`/`glDrawArrays`, que es el caso de Zenonia 2).

Se investigó extensamente, descartando en orden (cada uno probado y revertido si no cambiaba el resultado):
1. **Estrategia de memoria de `so_util.c`** — se probó reescribir la carga del `.so` para usar kubridge (`kuKernelAllocMemBlock`/`kuKernelCpuUnrestrictedMemcpy`/`kuKernelFlushCaches`) con la variante `EMULATOR_BUILD` copiada del proyecto de Prince of Persia. Resultado: esas 3 funciones de kubridge **no están implementadas en esta versión de Vita3K** (confirmado viendo `Import function for NID 0x2EF7C290/0x91D9CABC/0x38B70744 not found` en el log de Vita3K — coinciden exactamente con los NIDs de esas 3 funciones según `../Prince of Persia/lib/kubridge/exports.yml`), por lo que ese camino silenciosamente no cargaba nada real (sin crash, pero sin juego funcional tampoco). **Se revirtió `so_util.c`/`so_util.h` a la versión original** (que ya maneja `R_ARM_NONE` correctamente y usa una única reserva de 16MB sin kubridge) porque el crash de vitaGL **ocurría igual con la estrategia de memoria original**, antes de tocar nada de esto — es decir, esta investigación fue un callejón sin salida, no la causa.
2. **`ATTRIBUTE2` del `param.sfo`** — igualado al de Prince of Persia (`0xC`), reinstalado el VPK completo para que Vita3K lo tomara. Sin cambios en el crash.
3. **Backend de render** (`-B OpenGL` vs `-B Vulkan`) — sin cambios; tiene sentido, porque SceShaccCg se invoca igual sin importar el backend elegido para Vita3K.
4. **¿El crash depende de llamar a `gl_init()` siquiera?** Se probó comentando la llamada por completo — **el mismo crash exacto reapareció en otro punto del arranque** (durante la carga de módulos de sistema estándar, antes incluso de que corra el código del juego), lo cual ya era una señal de que el problema no estaba atado a una línea específica de nuestro código.
5. **Prueba de control decisiva:** se volvió a correr Prince of Persia (`POPC00001`), que **había funcionado perfectamente al principio de esta sesión** (shaders compilados 4/4, sin crash) — y para el final de la sesión **también crasheaba con la misma dirección exacta**. Esto confirma que es una **inestabilidad de la sesión de Vita3K** (probablemente por las decenas de arranques/cierres forzados seguidos durante esta depuración, degradando algún estado de caché de shaders/GPU/Metal compartido), **no un bug de este port ni de Zenonia 2 específicamente**.

**Conclusión:** el código actual (`gl_init()` habilitado, con la config sin MSAA/sin triple buffer que se sabe funciona en Prince of Persia) es lo correcto para intentar en **consola real**, donde no existe el dispatcher `call_import` de Vita3K que está crasheando. Vita3K en esta máquina, en este momento, no es una plataforma de prueba confiable — reiniciar Vita3K (o la Mac) antes de reintentar ahí, pero **la prioridad ahora es probar en hardware físico**, no seguir depurando el emulador.

### 9.5 — Estado del build al cierre de esta sesión

- Compila limpio con `cmake . && make` en `~/zenonia2-build` (symlink `~/zenonia2-src` → este repo, necesario porque `PSVITA Develop` tiene un espacio en el nombre — ver `toolchain_gotchas.md` de la skill).
- `build/zenonia_2.vpk`, `build/zenonia_2.self` y `build/zenonia_2.elf` (con símbolos, para analizar futuros `.psp2dmp` con `vita-parse-core`) actualizados con este build.
- **No verificado en consola real todavía** — es el siguiente paso obligatorio.
- `ENABLE_VERBOSE_JNI_LOG` sigue en `ON` — bajarlo a `OFF` (o pasar `-DENABLE_VERBOSE_JNI_LOG=OFF` a cmake) antes de una prueba de rendimiento real, dejarlo prendido solo para la primera tanda de debugging en consola.

## Siguiente Paso Real
1. Copiar `build/zenonia_2.vpk` + la carpeta `ux0_data/zenonia-2/` a una PS Vita real (FTP con VitaShell, como se documenta en la skill `hardware_debugging.md`).
2. Revisar `ux0:data/zenonia-2/log.txt` tras el primer arranque — buscar la última línea antes de un corte (crash) o un patrón que se repite sin avanzar (freeze). El log ya incluye: símbolos resueltos, entrada a `JNI_OnLoad`/`NativeInit`/`NativeResize`, y un latido cada 120 frames del loop principal.
3. Si crashea, bajar el `.psp2dmp` generado en `ux0:data/` y analizarlo con `vita-parse-core` contra `build/zenonia_2.elf` (ver `hardware_debugging.md` de la skill para el procedimiento exacto).
4. Recién con el juego llegando al menú en hardware real, retomar la **Fase: Audio** (pendiente, ver `plan_zenonia_port.md` §7).
