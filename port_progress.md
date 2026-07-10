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

### 9.6 — Assets de LiveArea corregidos y primera instalación real (2026-07-08, sesión siguiente)

- **`icon0.png`/`pic0.png`/`startup.png`/`bg0.png`** estaban en dimensiones y formato incorrectos (170x170 RGB truecolor en vez de las medidas exactas que exige la consola — `128x128`/`960x544`/`280x158`/`840x500` respectivamente — y sin paleta indexada de 8 bits). Regenerados con Pillow (`convert('P', palette=Image.ADAPTIVE, colors=256)`, recorte centrado para no distorsionar el aspecto original) a partir del material gráfico del juego. `bg0.png` no estaba ni siquiera enlazado: `template.xml` referenciaba `bg.png` (typo) y `CMakeLists.txt` no lo incluía en `vita_create_vpk`.
- **Error de instalación `0x80104004`** (`SCE_LIVEAREA_ERROR_XML_FORMAT`, código de usuario `C2-10045-0`): `template.xml` usaba `<livearea-title><text>...</text></livearea-title>`, que no es un elemento válido del esquema de LiveArea, y le faltaban los atributos `format-ver`/`content-rev`. Corregido con la estructura real (`<gate><startup-image>...</startup-image></gate>` + esos atributos), copiando la de Prince of Persia (`extras/livearea/template.xml`), que sí instala. **Con esto la instalación en consola real funcionó.**
- **Defaults de CMake corregidos:** `EMULATOR_BUILD` bajado a `OFF` por defecto (consola real es el target por defecto; ya no hacía nada de todos modos, ver más abajo) y `ENABLE_VERBOSE_JNI_LOG` bajado a `OFF` por defecto (evita la regresión de rendimiento de Fixes_Log #12-13 de Prince of Persia).

### 9.7 — Bug real #3: memoria del `.so` sin permiso de ejecución (crash en consola real, confirmado con `vita-parse-core`)

Con la instalación resuelta, el primer arranque en hardware real crasheó justo después de "Llamando JNI_OnLoad...". El `.psp2dmp` generado por la consola, analizado con `vita-parse-core` contra `build/zenonia_2.elf`, mostró:

- **Excepción:** Prefetch abort (fallo al *buscar una instrucción*, no al leer/escribir datos).
- **PC:** `0x89562b74` — exactamente la dirección de `JNI_OnLoad`, la función a la que se estaba saltando (`blx r3` en la instrucción anterior, con `r3` apuntando ahí).

Es decir: el CPU intentó ejecutar la primera instrucción del `.so` recién cargado y la página de memoria no tenía permiso de ejecución. La causa: al revertir `so_util.c`/`so_util.h` a la versión original en la sección 9.4 (para descartar la investigación de Vita3K), quedó activa la reserva de memoria **sin kubridge** (`sceKernelAllocMemBlock` con un tipo de bloque que es solo lectura/escritura). En Vita3K esto no se nota porque su emulador de CPU no aplica permisos de página (por eso el "descarte" de la sección 9.4 point 1 fue válido *para ese crash puntual de Vita3K*, pero dejó una regresión real para consola física, donde el hardware sí exige W^X). En hardware real, sin memoria genuinamente ejecutable, cualquier intento de correr el código cargado dinámicamente falla con exactamente este Prefetch Abort.

**Solución:** se restauró la implementación completa de `so_util.c`/`so_util.h` de Prince of Persia (con soporte real de `kubridge` — `kuKernelAllocMemBlock` con `SCE_KERNEL_MEMBLOCK_TYPE_USER_RX` para el segmento de texto, más `kuKernelCpuUnrestrictedMemcpy`/`kuKernelFlushCaches` para escribir y sincronizar esa memoria), esta vez **para quedarse**: es la versión correcta tanto para consola real (kubridge real, vía `EMULATOR_BUILD=OFF`, el default actual) como para un futuro intento en Vita3K (`EMULATOR_BUILD=ON`, arena única sin kubridge). Confirmado en `Prince of Persia/Docs/INSTALL_HARDWARE.md`: *"kubridge instalado... necesario en hardware real (a diferencia de Vita3K, que no lo requiere)"* — exactamente la asimetría que causó este bug.

**Requisito en consola:** `kubridge.skprx` debe estar cargado por taiHEN (`ur0:tai/config.txt`) — normalmente ya viene con CFW modernos (HENkaku/h-encore²/Ensō). Sin él, el mismo Prefetch Abort va a repetirse.

**Sobre la pantalla negra de debug tras el splash:** es esperada dado este bug — el crash ocurre *antes* de que el motor del juego ejecute una sola instrucción propia, así que lo único que llegó a verse fue el texto de debug de nuestro propio loader (`psvDebugScreenPrintf`), nunca el renderizado real del juego. Debería desaparecer una vez que `JNI_OnLoad`/`NativeInit` corran sin crashear.

**Sobre el `.apk` (`zenonia2.apk`):** no es necesario actualmente. A diferencia de Prince of Persia (que usa `nativeSetPaths` de cocos2d-x para abrir su `.apk` como zip y leer `assets/appConfig.txt` de ahí), el motor de Zenonia 2 (Gamevil Nexus) no llama a nada equivalente — todo el acceso a archivos pasa por `fopen_hook`/`stat_hook`/`access_hook` en `dynlib.c` (redirigen a `ux0:data/zenonia-2/assets/`) y por `readAssets` en `java.c` (lee directo de `ux0:data/zenonia-2/`), ninguno de los dos abre un `.zip`/`.apk`. No hace daño dejarlo copiado en la carpeta por si algún flujo no explorado todavía lo llegara a necesitar.

### 9.8 — El fix de kubridge funcionó: el crash avanzó a `CMvResourceMgr::LoadAllPTCData()` (Data abort, sin resolver todavía)

Confirmado: el fix de 9.7 sirvió — el juego ya pasa de `JNI_OnLoad` y entra a `NativeInit`. El siguiente `.psp2dmp` (analizado igual con `vita-parse-core`) muestra un crash distinto:

- **Excepción:** Data abort (esta vez es acceso a datos, no fetch de instrucción — o sea, ya no es el bug de 9.7).
- **PC:** `0xe0000216` (`SceLibKernel@1 + 0x16`) — dentro de un módulo real de Sony, no de nuestro código.
- **LR:** `0x980d647f` → resuelto contra los símbolos del propio `libzenonia2.so` (`arm-vita-eabi-nm -D --defined-only` + `c++filt`, restando `text_base`): `CMvResourceMgr::LoadAllPTCData() + 0x8f`.
- En la pila, justo antes del crash, aparece literalmente el string `"ptc/000."` — coincide con el patrón ya visto en la primera sesión (`isAssetExist` pedía `ptc/000.ptc`, `ptc/001.ptc`, etc.).

**Lectura:** el motor está cargando datos de partículas (`.ptc`) y en algún punto de ese proceso termina llamando a una función real de `SceLibKernel` (probablemente vía nuestros hooks `fopen_hook`/`stat_hook`/`access_hook` en `dynlib.c`, que reenvían a la implementación real de `fopen`/`stat`/`access` de vitasdk) — y esa llamada al kernel falla con un acceso a datos inválido. Sin el log de esta corrida específica no se puede confirmar cuál de los tres hooks fue el último en ejecutarse antes del crash, así que no hay fix todavía — necesita el log de la sección 9.9 para seguir la metodología de "un bug a la vez".

Hipótesis a confirmar con el próximo log (no descartadas todavía, en orden de sospecha):
1. **Mismatch de ABI en `struct stat`:** si el motor (compilado contra bionic de Android) reservó el buffer de salida de `stat()` con el tamaño/layout de bionic, y nuestro `stat_hook` reenvía a la implementación real de vitasdk/newlib (con un `struct stat` de tamaño/layout distinto), el kernel podría fallar al escribir el resultado en un puntero que no tiene el espacio/alineación esperada.
2. Puntero de ruta corrupto por algún límite de `translate_path` (buffer de 256 bytes) si la ruta original es inusualmente larga.
3. Algo específico del archivo `ptc/000.ptc` en particular (¿existe realmente en `ux0:data/zenonia-2/assets/ptc/000.ptc`? Confirmar que los assets subidos incluyen esa carpeta).

### 9.9 — Sistema de logs mejorado: un archivo por corrida, con timestamp, en su propia carpeta

A pedido del usuario: `init_log()` en `main.c` ahora crea `ux0:data/zenonia-2/logs/` (vía `sceIoMkdir`, falla en silencio si ya existe) y genera un archivo nuevo por ejecución, nombrado `log_<timestamp-unix>.txt` (usando `time(NULL)`) — antes se pisaba siempre el mismo `ux0:data/zenonia-2/log.txt`, lo que ya había causado confusión en esta misma sesión (bajar sin querer un log viejo pensando que era de la corrida actual — exactamente el problema que `hardware_debugging.md` de la skill advierte). Con esto se puede llevar control de varias corridas de prueba sin perder el historial.

### 9.10 — Causa real de 9.8: `isAssetExist` sin registrar devolvía `-1` en vez de `0`

El log con timestamp (9.9) resolvió la duda de 9.8 de inmediato: la última actividad antes del corte no era `fopen_hook`/`stat_hook`/`access_hook` (ninguno de los tres aparece en el log) — eran **3 llamadas a `GetStaticMethodID(..., "isAssetExist", ...)`, las tres "not found"**, seguidas del crash. `isAssetExist` nunca estuvo registrado en la tabla de FalsoJNI (`loader/java.c` solo registraba `readAssets`).

La causa exacta: `FalsoJNI_ImplBridge.c`'s `methodIntCall()` devuelve **`-1`** (no `0`) cuando el `methodID` no se encuentra. El motor llama a `isAssetExist` esperando un booleano C (`0` = no existe, no-`0` = existe) — al recibir `-1` (que es no-cero), interpreta que el archivo **sí existe**, y sigue adelante intentando cargarlo. Como `ptc/000.ptc` (y los siguientes) en realidad nunca se resolvieron, el motor termina en una ruta de código no preparada para esto (probablemente una asignación de memoria con un tamaño/índice derivado de ese resultado falso) y crashea dentro de una llamada al kernel — exactamente el `CMvResourceMgr::LoadAllPTCData()` que ya habíamos resuelto en 9.8 vía `vita-parse-core`, ahora con la pieza que faltaba del log.

**Solución (`loader/java.c`):** se registró `isAssetExist` como `METHOD_TYPE_INT` en `nameToMethodId[]`/`methodsInt[]`, implementado con una función `zenonia_resolve_asset_path()` compartida que prueba `ux0:data/zenonia-2/<nombre>` (la convención que ya usaba `readAssets`) y, si no existe ahí, cae a `ux0:data/zenonia-2/assets/<nombre>` (la convención que usan los hooks de `dynlib.c`) — ninguna de las dos convenciones estaba confirmada todavía por log real, así que se prueban ambas y se recuerda cuál funcionó. `readAssets` se actualizó para usar la misma función, así los dos nunca quedan en desacuerdo sobre si un archivo existe. Con esto, `isAssetExist` ahora devuelve `0`/`1` reales en vez de `-1` por default.

**Nota general:** cualquier otro método no registrado que el motor use como valor de retorno de tipo `int`/`boolean`/`byte`/etc. (no `void` ni `Object`, esos sí devuelven `0`/`NULL` de forma segura al no encontrarse) tiene el mismo riesgo. Si aparece un crash nuevo después de una tanda de "method ID not found" en el log, revisar primero si el tipo de retorno de ese método es uno de los que devuelve `-1`.

### 9.11 — El siguiente log/dump ya venía de la misma corrida que 9.10 (typo real `readAssete` vs `readAssets`)

El log `log_1783561332.txt` mostró algo nuevo después del fix de `isAssetExist`: `isAssetExist` ya funcionaba (`-> 1`), pero la última línea antes del crash era `GetStaticMethodID(..., "readAssete", ...)`: **not found**. `strings` sobre `libzenonia2.so` confirmó que **"readAssete" es un typo real del binario original** (existen las dos cadenas, "readAssets" Y "readAssete", en el propio `.so`) — nunca fue un problema de nuestro lado, el motor de verdad pide el nombre mal escrito.

Como no estaba registrado, `methodObjectCall` devolvió `NULL` (ver 9.10 — los métodos `Object` sí devuelven `NULL` de forma segura, a diferencia de los `int`). El `.so` tiene su propia función C++ `readAssets()` (confirmada con `nm -D`, offset `0x5440c`) que internamente hace la llamada JNI a "readAssete" y después usa el resultado así (desensamblado con `objdump -d`, función `MC_knlGetResource`):
```
bl   readAssets          ; r0 = resultado (NULL, porque "readAssete" no se encontró)
adds r1, r0, #0
adds r1, #16              ; r1 = NULL + 16 = 0x10
...
blx  memcpy@plt           ; memcpy(dest, src=0x10, len) -- lee desde una dirección casi nula
```
El `.psp2dmp` de esa misma corrida (`psp2core-1783561337`) confirma exactamente esto: crash dentro de `memcpy`, llamado desde `MC_knlGetResource`, con el puntero fuente roto.

**Ya estaba resuelto en el momento de analizar este dump** — la sección 9.10 (registrar `readAssete` además de `readAssets` en `loader/java.c`, apuntando al mismo handler) se implementó *antes* de leer este log/dump específico, así que el build que ya se entregó al usuario para la siguiente prueba ya incluye el fix. No hizo falta ningún cambio adicional de código en esta sección — solo quedó documentado como confirmación de que el mismo fix cubre ambos síntomas (9.10 vía el log, 9.11 vía el dump).

### 9.12 — `ftell()` devolvía basura (bytes de la propia ruta) como tamaño de archivo

Con `readAssete` resuelto, el siguiente log mostró `readAssets` ejecutándose de verdad por primera vez — y fallando: `[FakeJNI] MALLOC FAILED FOR SIZE 1952539695`. Ese número, decodificado a bytes (`hex(1952539695) = 0x7461642f`), son literalmente los caracteres ASCII `"/dat"` — un fragmento de la propia ruta del archivo (`ux0:data/zenonia-2/assets/com/light80x50.zt1`), no un tamaño real. El `.psp2dmp` de esa corrida confirma el mismo valor (`0x7461641f`/`0x7461642f`) en los registros, y resuelve el `LR` a `MC_knlCalloc + 0x21` — la asignación corrupta se propaga al motor un paso más adelante y ahí sí crashea duro.

**Causa:** `Zenonia_readAssets()` calculaba el tamaño con `fseek(f, 0, SEEK_END); size = ftell(f);` — por algún motivo (no confirmado con certeza, pero no relevante ya que la solución es robusta contra la causa exacta) `ftell()` devolvía basura para este archivo específico en vez del tamaño real.

**Solución (`loader/java.c`):** se reemplazó el cálculo por `fstat(fileno(f), &st)` (más directo, no depende de que el cursor del archivo esté bien posicionado) y se agregó un límite de cordura (`> 64 MB` se rechaza y loguea en vez de intentar el `malloc`) — así, si `ftell`/`fstat` vuelve a fallar para algún otro archivo, el resultado es un asset que no carga (recuperable) en vez de un crash duro corrompiendo el motor.

### 9.13 — `libshacccg.suprx` ya está instalado correctamente en la consola

El usuario preguntó si hacía falta subir `libshacccg.suprx` (el compilador de shaders Cg que necesita `vitaGL`). **No hace falta** — todos los logs de esta sesión muestran `"vitaGL inicializado."` sin ningún diálogo de error, y `Prince of Persia/Docs/INSTALL_HARDWARE.md` documenta que sin ese archivo el juego muestra un diálogo explícito y no arranca en absoluto. Como eso nunca pasó, `ur0:data/libshacccg.suprx` ya está presente y funcionando en esa consola.

## Siguiente Paso Real
1. Volver a probar en consola con el build actual (incluye los fixes de `isAssetExist`, `readAssete`, y el tamaño de archivo corrupto en `readAssets`). Bajar **toda** la carpeta `ux0:data/zenonia-2/logs/` y el `.psp2dmp` nuevo si vuelve a crashear.
2. Seguir la metodología de "un bug a la vez": mirar la última línea del log antes del corte.
   - "method ID N not found!" para un método `int`/`boolean`/`byte`/`short`/`float`/`double`/`long` → mismo problema de 9.10 (devuelve `-1`) con un método distinto → registrar en `methodsInt[]`/`methodsBoolean[]`/etc.
   - `GetStaticMethodID(..., "NOMBRE", ...): not found` para un método `Object` → mismo problema de 9.11 (el `.so` usa el `NULL` resultante sin chequear) → registrar en `methodsObject[]`, revisar con `strings`/`nm` si el nombre real tiene un typo.
   - `"[Java] readAssets: bogus/oversized size ..."` → el archivo en cuestión probablemente no está subido correctamente, o hay otro archivo con el mismo problema de 9.12 — confirmar que existe en la consola con el tamaño esperado.
3. Repetir el análisis con `vita-parse-core` para cualquier crash nuevo (`~/vita-tools/vita-parse-core/venv/bin/python3 main.py <dump> build/zenonia_2.elf`); si hace falta más detalle, desensamblar la función del `LR` con `arm-vita-eabi-objdump -d lib/libzenonia2.so --start-address=<addr> --stop-address=<addr+0x40>`.
4. Recién con el juego llegando al menú en hardware real, retomar la **Fase: Audio** (pendiente, ver `plan_zenonia_port.md` §7).

---

## Fase 10: Renderizado y Pantalla Blanca (En Progreso)

El juego ha logrado llegar a su bucle principal (`NativeRender`), pero la pantalla en la PS Vita se muestra completamente en blanco. Para solucionar esto, hemos comenzado una extensa depuración del pipeline de OpenGL:

### 10.1 — Diagnóstico del Pipeline de OpenGL y Hooks
- Se implementaron wrappers exhaustivos en `loader/dynlib.c` para registrar (loguear) cada llamada crítica de OpenGL (`glClear`, `glTexImage2D`, `glTexSubImage2D`, `glDrawArrays`, `glEnable`, `glDisable`, `glTexEnvf`, `glTexParameterx`, `glTexCoordPointer`, `glVertexPointer`).
- **Problema 1: Texturas GL_RGB con formato GL_UNSIGNED_SHORT_5_6_5.** 
  El motor original subía texturas en formato RGB565. En las pruebas iniciales, `vitaGL` reportaba errores `GL_INVALID_ENUM` (500) porque no soporta esa combinación nativamente de la misma forma que el motor lo espera.
  **Solución:** Se implementó en el wrapper de `glTexImage2D` y `glTexSubImage2D` una función `convert_rgb565_to_rgba8888` que convierte el formato de la textura en CPU a `GL_RGBA` de 32-bits (8888) antes de enviarla a `vitaGL`. Esto eliminó los errores GL.

### 10.2 — El problema de la "Textura Incompleta"
A pesar de solucionar el formato, la pantalla continuaba blanca. Al revisar los logs notamos:
1. El modo de textura es correcto (`GL_TEXTURE_ENV_MODE` = `GL_REPLACE`).
2. El motor de juego llama a `glTexImage2D` pero **NUNCA** llama a `glTexParameterx` para establecer los filtros de minificación/magnificación.
- **Causa:** En OpenGL (y `vitaGL`), si una textura no provee mipmaps y su `GL_TEXTURE_MIN_FILTER` por defecto requiere mipmaps (`GL_NEAREST_MIPMAP_LINEAR`), la textura se considera "incompleta" y se renderiza como blanco puro.
- **Solución:** Modificamos el wrapper `glTexImage2D_wrapper` para inyectar forzosamente `glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR)` y `GL_TEXTURE_MAG_FILTER`.

### 10.3 — Punteros de Atributos sin Client States Activados
El motor llamaba a `glTexCoordPointer` y `glVertexPointer`, pero mediante `nm -u` confirmamos que el juego sí importaba `glEnableClientState` pero, sorprendentemente, parecía no invocarlo (o asumir que el puente en Java de Android lo había encendido de antemano). Si los Client States están apagados, `vitaGL` ignora las coordenadas y no dibuja los sprites.
- **Solución:** Modificamos los wrappers de `glTexCoordPointer` y `glVertexPointer` para forzar `glEnableClientState(GL_TEXTURE_COORD_ARRAY)` y `glEnableClientState(GL_VERTEX_ARRAY)` inmediatamente después de registrar el puntero, asegurando que `vitaGL` realmente lea los buffers.
- Además se incluyó un pequeño dumpeo en hexadecimal de los primeros 4 píxeles en `glTexSubImage2D` para constatar que el motor de juego está extrayendo información no blanca desde los `.zt1` / `.pzx` assets.

**Próximo Paso Real:**
Esperar la validación del usuario en su PS Vita con estas 3 inyecciones gráficas forzadas para ver si el motor comienza a renderizar visuales. Paralelamente, de confirmarse un avance o revelarse un requerimiento de audios, el próximo hito principal será la interpolación de efectos sonoros (`108.mmf`).

### 10.4 — Vértices `GL_FIXED` (Q16.16) pasados sin convertir a vitaGL (2026-07-09)

Los logs de la noche del 8-9 de julio (`log_1783568650.txt` en adelante) mostraron que el motor **no estaba
colgado**: el loop corría miles de frames (`frame 481 alive...`) con llamadas GL continuas, pero nada se
dibujaba. La pista estaba en el dump de vértices del wrapper:

```
[GL] glVertexPointer size=2 type=140c stride=0 pointer=0x8147e9d8
  -> Verts (first 6): (-31457280, -17825792) (31457280, -17825792) ...
```

`type=140c` es **`GL_FIXED`** — punto fijo Q16.16, típico de motores viejos derivados de J2ME/BREW como
este. `31457280 = 480 × 65536` y `17825792 = 272 × 65536`: es un quad de pantalla completa (±480, ±272)
en formato fixed-point, perfectamente válido… que vitaGL no interpreta (lee los enteros crudos como si
fueran floats → geometría absurda, fuera del frustum → nada en pantalla). El mismo tipo de problema que ya
teníamos identificado para `glClearColorx`/`glTexParameterx`, pero que se había escapado en `glVertexPointer`.

**Solución (`loader/dynlib.c`):** `glVertexPointer_wrapper` detecta `type == GL_FIXED` y **difiere** la
conversión (ahí no se sabe cuántos vértices hay); `glDrawArrays_wrapper` (único draw call que usa este
juego) convierte los vértices a `GL_FLOAT` (`valor / 65536.0f`) en un buffer que crece bajo demanda, justo
antes de dibujar. De paso se conectaron en `default_dynlib[]` dos wrappers que existían pero nunca se habían
registrado: `glClearColorx` y `glTexParameterx`.

### 10.5 — Diagnóstico definitivo con `sceDisplayGetFrameBuf`: vitaGL SÍ presenta, el contenido es blanco

Para dejar de adivinar "¿se presenta el buffer de vitaGL o se queda el de debugScreen?", se agregó a
`main.c` un diagnóstico que loguea la dirección real del framebuffer escaneado a pantalla
(`sceDisplayGetFrameBuf`) en tres momentos: al arrancar (baseline), después de `gl_init()`, y cada ~2s en
el loop. Resultado en consola real (logs `log_1783634751/5071/5377.txt`):

```
[DISPLAY] baseline:  base=0x61000000   (debugScreen)
[DISPLAY] main loop: base=0x626a5000   (vitaGL — el swap SÍ tomó el display)
```

Conclusión: el pipeline gráfico funciona de punta a punta. La pantalla blanca es **contenido que el juego
mismo dibuja**. Confirmado por el dump de píxeles: el motor renderiza por software a un buffer interno de
**400×240 RGB565** (la resolución WQVGA del juego original de teléfono), lo sube con `glTexSubImage2D` y lo
dibuja en el quad de 10.4 — y ese buffer viene 100% blanco (`convert_rgb565_to_rgba8888: min=ffff max=ffff`).

**Curva de aprendizaje que vale registrar:** en el camino se probó tratar el retorno `GL_FALSE` de
`vglInitExtended` como error fatal — **incorrecto y revertido**. Ese booleano solo indica si la resolución
pedida tuvo que reducirse para entrar en el display (`res_fallback` en el código fuente de vitaGL); a
960×544 (resolución nativa) siempre devuelve `GL_FALSE` y eso es lo sano. No tratarlo como fallo de init.

### 10.6 — Causa raíz de la pantalla blanca: el logo y el título son UI DE JAVA, no del motor nativo

Con el pipeline descartado, la pregunta pasó a ser "¿por qué el motor dibuja blanco?". La respuesta salió de
**decompilar el `classes.dex` del APK con jadx** (ver §10.8): el flujo del log encaja exactamente con la
máquina de estados de UI del wrapper Java de Gamevil:

- El log muestra `OnUIStatusChange: 0` (logo) → carga `menu/logo.pzx` + `OnSoundPlay id=0` → luego
  `OnUIStatusChange: 1` (título) + `OnSoundPlay id=108 loop=50` (música del título en loop). **El juego
  avanza solo hasta el título y se queda esperando input ahí.** No está colgado.
- En `Zenonia2UIControllerView.setUIState()` (decompilado): status 0 → `showLogView()` muestra
  `logImageView`; status 1 → `showTitleView()` muestra `titleImageView`. Ambos son **`ImageView` de
  Android** — bitmaps de `res/` dibujados por el view system de Android POR ENCIMA de la superficie GL.
  El motor nativo solo pinta el fondo blanco debajo. Como nuestro loader no emula esa capa de vistas Java,
  lo único visible es el blanco del nativo.
- Para avanzar del título hace falta el "touch to start" (un `UIFullTouch` de pantalla completa) — lo que
  llevó directo al siguiente hallazgo: nuestro input estaba mal implementado en tres ejes (§10.7).

**Implicación futura:** si después de pasar el título el menú también se ve blanco, significa que esa
pantalla también es UI Java (`UITexturePlane`/ImageViews) y habrá que dibujar los bitmaps de
`apk_extract/res/` desde el loader (con vitaGL, sabiendo el `uiStatus` actual vía el callback
`OnUIStatusChange` que ya interceptamos en `java.c`).

### 10.7 — Protocolo de input real del APK (touch y botones) — reescritura de `main.c`

Decompilando `NexusGLRenderer`, `NexusHal`, `UIFullTouch` y `Zenonia2UIControllerView` (§10.8) quedó
documentado el protocolo de entrada EXACTO que espera `libzenonia2.so` — y lo que teníamos en `main.c`
estaba mal en tres cosas a la vez:

| Aspecto | APK real (Java decompilado) | `main.c` (antes del fix) |
|---|---|---|
| Touch down | evento **23** (`MH_POINTER_PRESSEVENT`, NexusHal.java) | tipo 0 |
| Touch up | evento **24** (`MH_POINTER_RELEASEEVENT`) | tipo 1 |
| Coordenadas | espacio interno del juego **400×240** (`UIFullTouch.convertScreenX/Y`: `x*400/width`) | 960×544 |
| Entrega | **doble**: `setInputEvent(...)` inmediato al evento **+** `handleCletEvent(...)` justo antes del siguiente `NativeRender` (`NexusGLRenderer.drawFrame` → `sendHandleCletEvent()`) | solo `setInputEvent` |
| Teclas | press=**2** / release=**3** (`MH_KEY_PRESSEVENT/RELEASEEVENT`) con keycodes HAL | sin mapear |

Keycodes HAL (de `Zenonia2UIControllerView.getHalKeyCode()` y los `UI*Button.java`): **↑ -1, ↓ -2, ← -3,
→ -4, OK/confirmar -5, mapa -6, reset -8, save -10, back/menú -16, skip/quickslot 35**. En el teléfono
estos los generaba la UI táctil de Java (dpad y botones en pantalla) — en Vita los generamos desde los
botones físicos, que es objetivamente mejor.

**Implementado en `main.c`:**
- Touch: eventos 23/24 con coordenadas `x*400/1920`, `y*240/1088` (panel táctil → espacio del juego). El
  release manda las últimas coordenadas conocidas, igual que Java. `UIFullTouch` no manda eventos de move,
  así que nosotros tampoco.
- Botones (press/release por flanco, tabla `btn_map[]`): D-Pad → direcciones, Cruz → OK (-5),
  Círculo → back/menú (-16), Triángulo → skip (35), Cuadrado → mapa (-6), L → save (-10).
- Cola FIFO de eventos: cada evento dispara `setInputEvent` al instante Y se encola; el loop entrega **uno
  por frame** vía `handleCletEvent` justo antes de `NativeRender` (mismo orden que `drawFrame` en Java).
- **La salida de emergencia ahora es START+SELECT juntos.** Antes era START solo (`if (pad.buttons &
  SCE_CTRL_START) break;`) — eso explicaba el reporte de "el juego se cierra sin error al apretar Start":
  no era un bug del juego, era nuestro propio código de salida.
- Los primeros 40 eventos se loguean como `[INPUT] event type=... p1=... p2=...` para verificar el mapeo
  en el próximo log de consola.

### 10.8 — REFERENCIA: decompilar el APK con jadx — la fuente de verdad del lado Java

Esta sección documenta la metodología que destrabó 10.6 y 10.7, para que cualquier persona o IA que retome
este port (o un port futuro de otro juego Android) la pueda repetir. **Cuando el comportamiento del motor
depende de qué hacía el wrapper Java del APK (input, UI, audio, callbacks, ciclo de vida), no adivinar:
decompilar y leer el código real.**

**Herramienta:** [jadx](https://github.com/skylot/jadx) — decompilador de DEX a Java legible. Ya está
instalado en esta máquina (`brew install jadx`).

**El decompilado YA ESTÁ HECHO y vive en el proyecto:** `apk_decompiled/sources/com/gamevil/...`
(excluido de git por derivar de material propietario — regenerable en segundos si se pierde):
```bash
jadx -d apk_decompiled apk_extract/classes.dex
# genera apk_decompiled/sources/com/gamevil/... con todo el código Java decompilado
```

**Clases clave de ESTE juego (motor Gamevil Nexus2 / "Clet") y qué responde cada una:**

| Clase | Qué documenta |
|---|---|
| `com/gamevil/nexus2/Natives.java` | La interfaz completa nativo↔Java: los `public static native` son lo que el loader debe llamar; los `private static` son los callbacks que el `.so` invoca vía JNI y que `loader/java.c` debe implementar (con esa firma exacta). |
| `com/gamevil/nexus2/NexusGLRenderer.java` | El orden del frame: `sendHandleCletEvent()` (un evento pendiente por frame) → `NativeRender()`. Nuestro loop principal replica esto. |
| `com/gamevil/nexus2/NexusHal.java` | Todas las constantes de evento `MH_*` (touch 23/24/25, teclas 2/3, etc.). |
| `com/gamevil/zenonia2/ui/UIFullTouch.java` | Cómo se convierten las coordenadas táctiles al espacio del juego (400×240) y qué eventos se mandan. |
| `com/gamevil/zenonia2/ui/Zenonia2UIControllerView.java` | `getHalKeyCode()` (mapa de teclas HAL), `setUIState()` (qué muestra cada `uiStatus` — crítico para 10.6), y los listeners `OnUIStatusChange`/`OnSoundPlay`/etc. |
| `com/gamevil/nexus2/ui/UI*Button.java`, `UIDirectionPad.java` | Qué keycode HAL manda cada botón táctil de la UI de Android (nuestra referencia para mapear los botones físicos de la Vita). |
| `com/gamevil/nexus2/ui/NexusSound.java` | (Pendiente — Fase Audio) cómo el lado Java reproduce los sonidos que `OnSoundPlay(id, vol, loop)` pide. |

**Método de trabajo que funcionó** (aplicable a cualquier síntoma nuevo):
1. Mirar el log de consola real: ¿cuál fue el último callback `[Java] ...` o el patrón que se repite?
2. Buscar ese método en los `sources/` decompilados (`grep -rn "OnSoundPlay" sources/`) y leer qué hacía
   el APK real con él — con qué argumentos, qué estado cambia, y **qué respuesta espera el nativo**
   (¿un evento de vuelta? ¿un valor de retorno? ¿nada?).
3. Replicar esa semántica exacta en `loader/java.c` (callbacks) o `loader/main.c` (ciclo de vida/input).
4. No confiar en suposiciones sobre convenciones Android genéricas: este motor usa códigos propios
   (`MH_*` de NexusHal) que NO son los `MotionEvent.ACTION_*` estándar de Android. Solo el decompilado
   lo revela.

**Otros datos del APK útiles ya confirmados:**
- `apk_extract/res/drawable*/` tiene los bitmaps de la UI Java (logo, título, dpad, botones) — la materia
  prima si hay que dibujar esa capa desde el loader (ver implicación en 10.6).
- `apk_extract/assets/` es el origen de `ux0:data/zenonia-2/assets/`.
- `AndroidManifest.xml` declara la activity real (`Zenonia2Launcher`) y sus flags — consultar si aparece
  algo de ciclo de vida raro (orientación, license check `armPassed` de `Zenonia2Launcher`, etc.).

**Próximo Paso Real (2026-07-09):**
1. Instalar el VPK nuevo (fixes de 10.4 + 10.7) en consola real.
2. En la pantalla blanca del título: **tocar la pantalla o apretar Cruz**. Debería dispararse el "touch to
   start" (evento 23/24 → `handleCletEvent`) y avanzar del título.
3. Bajar el log nuevo: verificar las líneas `[INPUT] event ...` y si aparece un `OnUIStatusChange` con un
   status nuevo (≠ 0/1) después del toque — eso confirma que el juego avanzó de estado.
4. Si avanza pero sigue blanco → el menú también es UI Java → implementar el dibujado de los bitmaps de
   `res/` desde el loader (plan en 10.6). Si avanza y SE VE → seguir hacia el menú/gameplay y retomar la
   Fase de Audio (`OnSoundPlay` → SceAudio, leyendo `NexusSound.java` decompilado como referencia).

### 10.9 — Decompilación de `libzenonia2.so` y Análisis del Renderizado Nativo (2026-07-09)

Para validar el pipeline y entender con certeza el origen de los buffers de texturas, se decompiló el binario `libzenonia2.so` utilizando una imagen de Docker con Ghidra Headless (`devrvk/so-decompiler`), generando el código fuente equivalente en C en `output/out_ghidra.c`.

**Hallazgos clave del código decompilado:**
- **`getDeviceInfo()` (Línea 1187):** Inicializa y retorna una estructura global de información del dispositivo (`di`). Reserva un búfer de píxeles (`malloc(0x80000)` para `di[0]`) y configura las dimensiones a **400x240** (`di[2]` a `di[7]`), correspondientes al tamaño original de pantalla de Zenonia 2.
- **`platformDrawBitmap(pixels)` (Línea 5157):** Recibe la dirección del búfer de píxeles actual y la almacena en el segundo miembro de la estructura (`di[1]`, a un offset de `+ 4` bytes).
- **`MC_grpFlushLcd` (Línea 4127):** Función interna del motor que llama directamente a `platformDrawBitmap` pasándole la dirección de memoria de la pantalla virtual.
- **`glDrawFrame()` (Línea 1210):** Función que se ejecuta en el bucle principal y sube el búfer de píxeles `di[1]` usando `glTexSubImage2D` en formato `GL_RGB` y tipo `GL_UNSIGNED_SHORT_5_6_5` (RGB565).
  
**Conclusión:**
Este análisis valida que nuestro wrapper de texturas es 100% correcto en formato (`RGB565` -> `RGBA8888`) y tamaño. También confirma que los bytes `0xFFFF` que vemos subirse al búfer provienen del propio motor del juego y no de un error de lectura o corrupción en el cargador. Actualmente, se ha compilado una versión con un **retraso de 4 segundos de depuración** en el inicio y **logs verbose de JNI** para monitorear qué está esperando el motor antes de pintar algo más allá de la pantalla en blanco.

---

## Fase 11: ¡El juego funciona! Menú alcanzado, partida iniciada — y fix del crash de mapa (2026-07-09, tarde)

**Hito confirmado en consola real:** con los fixes de la Fase 10 (vértices GL_FIXED + protocolo de input del
APK), el juego pasó del título, mostró el **menú (visible y navegable con los botones físicos)** y permitió
**iniciar una partida**. El renderizado nativo y el mapeo de teclas funcionan. Pendiente de probar: táctil.

### 11.1 — Crash al cargar el mapa de la partida: punteros del heap tratados como negativos

Al iniciar partida, crash (`log_1783637225.txt` + `psp2core-1783637289`). El log muestra la carga correcta
de `map/139.zt1` y `map/005.pzx` y se corta. `vita-parse-core` + `nm`/`c++filt` contra `lib/libzenonia2.so`:

- **PC:** `CMvMap::CreateMiniMap() + 0xaa` — `ldrb r1, [r1, #1]` con `R1 = 0` (Data abort leyendo la
  dirección 1). En la pila: `MC_grpCreateOffScreenFrameBuffer`, `MC_knlCalloc`.
- Desensamblando la cadena completa (`objdump -d` sobre `CreateMiniMap`, `CMvMap::Load`,
  `CMvMap::PreLoad`, `CMvLayerData::PreLoad`): `CreateMiniMap` recorre las celdas del mapa leyendo
  `this+0xe0` (array de `CMvLayerData`, stride 24) → `capa[0]+12` (array de atributos de celda,
  2 bytes/celda) — y ese puntero era **NULL**.
- **Causa raíz** en `CMvLayerData::PreLoad+0x1e` (VA `0xaec36`): `cmp r3, #0; ble <skip>` donde **r3 es el
  puntero al buffer del mapa pasado como `long` (con signo)**. Si el "puntero ≤ 0", saltea TODOS los
  callocs de capas (dejándolas NULL). En Android el heap del proceso vive en direcciones bajas
  (positivas) y el chequeo pasa siempre. En Vita, nuestro heap de newlib está en `0x81xxxxxx` — que como
  entero con signo es **negativo** — así que el motor "veía" un buffer inválido, no cargaba ninguna capa,
  y el minimapa crasheaba después. Un bug latente del juego original que solo se manifiesta con
  direcciones de heap altas.

**Solución — parche binario en memoria (`apply_so_patches()` en `loader/main.c`):** se cambia el `ble`
(`0xdd27`) por `beq` (`0xd027`) en `text_base + 0xaec38`, es decir "saltear solo si el puntero es NULL de
verdad". Se aplica con `kuKernelCpuUnrestrictedMemcpy` después de `so_relocate`/`so_resolve` y **antes** de
`so_flush_caches` (que sincroniza la caché de instrucciones). El parche **verifica los bytes originales**
antes de escribir (si el `.so` no coincide, loguea y no toca nada). Verificado: las 4 copias del `.so` en
el repo (`lib/`, `ux0_data/`, `apk_extract/`, `uploads/`) son idénticas (md5 `cae9d5fe...`).

**Patrón para el futuro:** si aparece otro crash con un puntero NULL "imposible" en datos que deberían
haberse cargado, sospechar del mismo patrón `cmp rX, #0; ble/bgt` sobre un puntero — buscar en el
desensamblado chequeos con signo alrededor de donde se debió asignar el dato. Solo se encontró UNO en esta
función (los otros `cmp #0` de la misma son sobre tamaños, inofensivos).

### 11.2 — Limpieza visual: sin textos de debug en pantalla + splash de bg0 en vez de pantalla blanca

A pedido del usuario:
- **`game_log` ya no imprime nada en pantalla** (solo al archivo de log). `psvDebugScreenInit()` ya no se
  llama al arrancar — solo dentro de `fatal_error()`, así un arranque sano nunca muestra texto de consola.
  También se quitó la espera de 4 segundos del arranque.
- **Splash:** `splash.rgba` (960×544 RGBA8888 crudo, generado desde
  `sce_sys/livearea/contents/bg0.png` con Pillow — regenerable con
  `python3 -c "from PIL import Image; open('splash.rgba','wb').write(Image.open('sce_sys/livearea/contents/bg0.png').convert('RGBA').resize((960,544),Image.LANCZOS).tobytes())"`)
  se empaqueta en el VPK (`FILE splash.rgba splash.rgba` en CMakeLists) y el loader lo dibuja como quad
  texturado **encima** del render del juego mientras `g_ui_status <= 1` (estados 0=logo y 1=título, que en
  Android eran ImageViews de Java y acá se ven blancos — ver §10.6). `g_ui_status` lo actualiza el callback
  `OnUIStatusChange` en `java.c`. Desde el estado 2 (menú) el motor dibuja contenido real y el splash se
  oculta solo. Si el juego vuelve al título, el splash reaparece — comportamiento correcto, el título
  sigue siendo invisible.
- Se quitó el clear rojo de debug (ahora negro) y el diagnóstico `sceDisplayGetFrameBuf` del loop (ya
  cumplió su función en §10.5). El log de frames ahora incluye `ui_status`.

### 11.3 — Stubs JNI nuevos para callbacks de gameplay

`OnStopSound`, `hideLoadingDialog` y `OnShowSaveButton` (UI/audio de Java, llamados durante el gameplay)
se registraron como no-ops void en `loader/java.c` — eran inofensivos (los métodos void no encontrados no
corrompen nada, ver §9.10) pero spameaban el log una vez por llamada. `OnStopSound` quedará real cuando se
haga la Fase de Audio.

### Próximo Paso Real (2026-07-09, tarde)
1. Instalar el VPK nuevo. Verificar: arranque sin textos → splash de bg0 → (Cruz para pasar el título
   invisible detrás del splash) → menú → **iniciar partida: ya no debería crashear al cargar el mapa**
   (buscar en el log `Parche aplicado: CMvLayerData::PreLoad`).
2. Probar la pantalla táctil en el menú/juego (protocolo 23/24 ya implementado, sin probar en consola).
3. Si el gameplay es estable: Fase de Audio (`OnSoundPlay`/`OnStopSound` → SceAudio; los `.mmf` faltantes
   sugieren convertir el audio del APK o mapear IDs a archivos propios — leer `NexusSound.java` +
   `apk_extract/res/raw/` como referencia).

---

## Fase 12: Audio (música y SFX) con sceAudioOut + Tremor (2026-07-09, noche)

**Confirmado por el usuario: el juego ya es jugable** (menú → partida → gameplay estable con botones).
Esta fase agrega lo único que faltaba: el sonido.

### 12.1 — Dónde está el audio y cómo lo mapea el APK

- Los archivos están en **`apk_extract/res/raw/`**: 74 `.ogg` (Vorbis, todos a 22050 Hz, mono o estéreo,
  ~14 MB) llamados `s000.ogg` … `s120.ogg`. Los `sound/NNN.mmf` que el motor consulta por `isAssetExist`
  (y no existen) son un camino legacy muerto — en Android el audio real NO pasa por assets, pasa por
  callbacks JNI a Java.
- `SoundMgr.java` (decompilado): mapea el `sndID` de `OnSoundPlay` → `res/raw/sNNN.ogg` (mismo número,
  con ceros a la izquierda) + un flag **isSFX** por ID. IDs SFX: 1-14, 17, 18, 20, 22, 33-36, 46-48, 67;
  el resto es música/stream.
- `NexusSound.java` (decompilado): tres canales — **SFX** (SoundPool: one-shots que se superponen),
  **BGM** (`playSound(id, isLoop=true)`: corta la música anterior y loopea), **stream** (`isLoop=false`:
  jingles de una pasada, corta el anterior). `OnStopSound` para todo.
- **Corrección importante**: la firma real es `OnSoundPlay(int sndID, int vol, boolean isLoop)` — el
  segundo parámetro es **volumen** (observado 50, escala ~0-100), el tercero es el loop. Los logs viejos
  los etiquetaban al revés (`loop=50 flag=1` era en realidad `vol=50 isLoop=true`).

### 12.2 — Implementación en el loader (`loader/audio.c` + `audio.h`)

- **Decodificación**: Tremor (`libvorbisidec` + `libogg`, ya precompilados en vitasdk — solo hubo que
  agregarlos a `target_link_libraries`). Decodifica OGG Vorbis en punto fijo, ideal para el ARM de la Vita.
- **Salida**: un único puerto `sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 512, 22050, STEREO)` y un
  **thread mezclador** propio: 6 voces (1 BGM + 1 stream + 4 SFX), cada una con su `OggVorbis_File`
  streameando del archivo; se suman con saturación y `sceAudioOutOutput` (bloqueante) marca el ritmo.
  Mono se duplica a estéreo; el loop de BGM es `ov_pcm_seek(vf, 0)` al llegar a EOF.
- **Despacho fiel al Java**: SFX → primera voz SFX libre (o pisa la primera); música con loop → reemplaza
  la voz BGM; sin loop → reemplaza la voz stream. `OnStopSound` cierra todas las voces.
- `java.c`: `OnSoundPlay` ahora llama `audio_play(id, vol, isLoop)` (log corregido) y `OnStopSound` pasó
  de no-op a `audio_stop_all()`. `main.c`: `audio_init()` después de `gl_init()`.

### 12.3 — Instalación de los archivos de audio en la consola

Los `.ogg` NO van dentro del VPK (material propietario — misma convención que los assets): van en
**`ux0:data/zenonia-2/sound/sNNN.ogg`**. En el repo ya quedaron copiados en `ux0_data/zenonia-2/sound/`
(74 archivos) — subir esa carpeta por FTP igual que se hizo con `assets/`. Si un archivo falta, el loader
loguea `[AUDIO] no encontrado: ...` (máx. 20 veces) y sigue sin crashear.

### Próximo Paso Real (2026-07-09, noche)
1. Subir `ux0_data/zenonia-2/sound/` → `ux0:data/zenonia-2/sound/` por FTP e instalar el VPK nuevo.
2. Verificar: música del título al arrancar (id 108 suena detrás del splash), SFX al navegar el menú,
   música de mapa al jugar. En el log: `[AUDIO] mezclador iniciado`, y ningún `[AUDIO] no encontrado`.
3. Probar el táctil (sigue pendiente de la Fase 11).
4. Si todo suena bien: primer commit de todo el trabajo acumulado (el repo tiene todos los cambios sin
   commitear desde el commit ef7e0f3).

### 12.4 — Crash al cargar partida guardada: mismatch de ABI en `struct stat` (bionic vs. newlib)

**Audio confirmado funcionando por el usuario.** Al cargar una partida guardada: crash
(`log_1783646521.txt` + `psp2core-1783646550`). El log lo delata en dos líneas:

```
[FakeJNI] stat_hook: ux0:data/zenonia-2//Save0.dat -> ...
[FakeJNI] MALLOC FAILED FOR SIZE 2167671008
```

`2167671008 = 0x81340CE0` — una dirección de nuestro heap usada como "tamaño" del save. Es exactamente la
hipótesis #1 que quedó anotada (y sin confirmar) en §9.8: **el motor está compilado contra bionic
(Android) y lee el `struct stat` con el layout de bionic, no el de newlib/vitasdk** que llenaba nuestro
`stat_hook`.

Confirmación por desensamblado (`MC_fsFileAttribute`, el ÚNICO call site de `stat` en todo el binario):
tras `blx stat@plt`, el motor lee `[sp, #16]` (st_mode: chequea el bit `0x4000` = S_IFDIR y los permisos
`0600`) y `[sp, #48]` (st_size). Offsets 16 y 48 = `struct stat` de bionic ARM 32-bit (NDK android-9). En
el `struct stat` de newlib esos offsets caen en otros campos/basura → el "tamaño" era stack sin
inicializar → `MC_knlAlloc` gigante → NULL → Data abort en la llamada a kernel siguiente.

**Solución (`loader/dynlib.c`):** `stat_hook` ahora hace `stat()` real con el struct de newlib y traduce
campo por campo a un `bionic_stat_t` (definido con los offsets exactos de bionic, verificados con
`_Static_assert(offsetof(...))` en tiempo de compilación) sobre el buffer del motor. Los bits de
`st_mode` (S_IFDIR, permisos) son valores POSIX idénticos en ambas libc, se copian tal cual.

**Patrón para el futuro (2ª aparición de esta clase):** cualquier función de libc que el motor importe y
que ESCRIBA UN STRUCT en un puntero del caller (`stat`, `gmtime`/`localtime` — ¡ambos importados y hoy
mapeados directo a newlib!—, `fstat` si apareciera) es sospechosa del mismo mismatch de ABI. `gmtime`/
`localtime` devuelven `struct tm`, cuyo layout SÍ coincide entre bionic y newlib (ambos siguen el C
estándar: 9 ints), así que esos están bien — pero verificar el layout antes de mapear cualquier import
nuevo que devuelva o llene structs.
