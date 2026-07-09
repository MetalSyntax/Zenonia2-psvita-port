# Plan de Port de Zenonia 2 para PS Vita

Este plan detalla los pasos necesarios para llevar a cabo el port de Zenonia 2 (Android) a PS Vita utilizando el `psvita-port-toolkit` (SoLoader Boilerplate) proporcionado, la librería `.so` del juego original y sus assets.

> **Actualización 2026-07-08 — leer antes de seguir:** los items marcados `[x]` en las secciones 4 y 5 estaban escritos a partir del código, no de una prueba real — el juego nunca se había ejecutado de punta a punta hasta esta fecha. Al probarlo por primera vez se encontraron y corrigieron dos bugs reales que impedían arrancar (FakeJNI incompleto que corrompía memoria, vitaGL nunca inicializado). El objetivo de prueba **cambió de Vita3K a consola real**: Vita3K demostró tener un crash propio del emulador al inicializar vitaGL (reproducido incluso con el port de referencia que sí funciona), no confiable como entorno de prueba en este momento. Ver `port_progress.md` Fase 9 para el detalle completo y los pasos siguientes concretos.

## 1. Configuración del Entorno y Preparación

- [ ] **Configurar VitaSDK:** Asegurar que las variables de entorno de VitaSDK están correctamente configuradas y actualizadas.
- [ ] **Dependencias:** Verificar que las librerías necesarias estén instaladas en el entorno (ej: `vitaGL`, `mathneon`, `kubridge`, `taihen`, etc.).
- [ ] **Estructura del Proyecto:** Integrar el código base de `psvita-port-toolkit` dentro de la carpeta actual y asegurar que el `CMakeLists.txt` apunte correctamente a los archivos fuente en `loader/`.

## 2. Análisis del Ejecutable Original (`.so`)

- [ ] **Inspección de la librería:** Usar herramientas como `readelf` o `objdump` (desde el toolchain de Android/Vita) sobre los archivos en `lib/` (presumiblemente `libzenonia2.so` o similar) para identificar sus dependencias y las llamadas al sistema operativo Android.
- [ ] **Funciones importadas y exportadas:** Mapear qué funciones requiere la librería (ej: OpenGL ES, OpenAL/OpenSL, JNI, libc, libm, liblog).

## 3. Carga Dinámica (SoLoader)

- [ ] **Mapeo de Memoria:** Usar `so_file_load` dentro del loader en C (`main.c` / `loader.c`) para cargar la librería dinámica en memoria en la Vita.
- [ ] **Resolución de Dependencias:** Relocalizar todas las funciones externas de la librería `.so` a sus equivalentes nativas en PS Vita usando la API de SoLoader (hooking y patching).
- [ ] **Manejo de Librerías Falsas (Fake Libraries):** Emular bibliotecas estándar de Android como `liblog`, `libm` o `libc` en caso de que presenten comportamientos incompatibles.

## 4. Implementación de Falso JNI (Java Native Interface)

- [x] **Entorno Falso JNI:** ~~estructura hecha a mano~~ → reemplazada por **FalsoJNI** (vendorizada en `lib/falso_jni/`, tomada del port de Prince of Persia) tras confirmar que la tabla manual de 300 slots solo cubría 3 funciones reales y corrompía memoria en el resto (`GetStringUTFChars` sin implementar → ver `port_progress.md` §9.1). Registrado solo `readAssets` en `loader/java.c`, que es lo único que este motor necesita del lado "Java".
- [x] **Inicialización (NativeInit):** confirmado — `Game_JNI_OnLoad`/`NativeInit`/`NativeResize` se resuelven y se llaman correctamente con `&jvm`/`&jni` reales de FalsoJNI.
- [ ] **Ciclo de Vida:** pausa/reanudación/destrucción (`NativePauseClet`, `NativeDestroyClet` vistos en el análisis de símbolos) — no implementado todavía, no bloquea llegar al menú.

## 5. Renderizado y Gráficos (vitaGL)

- [x] **Traducción OpenGL ES:** enlazado a `vitaGL` vía `so_resolve`'s fallback a `vglGetProcAddress` (ver `loader/so_util.c`) + wrappers de punto fijo en `dynlib.c` (`glClearColorx`, `glTexParameterx`).
- [x] **Configuración del Contexto:** `gl_init()` en `loader/main.c` ahora sí llama a `vglInitExtended(...)` (antes estaba comentado — ver `port_progress.md` §9.2). **Sin verificar en consola real todavía** — en Vita3K esta inicialización crashea, pero se confirmó que es un bug del emulador (reproducido con un port de referencia que sí funciona), no de este código — ver `port_progress.md` §9.4.
- [x] **Resolución de Pantalla:** 960x544 fijo, sin escalado adicional (el motor ya recibe `NativeResize(..., 960, 544)`).

## 6. Entradas (Controles y Táctil)

- [x] **Mapeo de Botones:** Leer la entrada física usando `sceCtrlReadBufferPositive`. Enviar estos eventos a la lógica del juego. Como Zenonia 2 era originalmente táctil pero con un pad virtual, puede requerir inyectar coordenadas táctiles o interceptar el mapeo del D-pad virtual hacia botones reales de la Vita.
- [x] **Entrada Táctil:** Habilitar el uso de `sceTouchRead` para soportar la pantalla táctil en menús, inyectando eventos equivalentes de "Touch Down", "Touch Move" y "Touch Up" vía el Falso JNI.

## 7. Audio (Música y SFX)

- [ ] **Intercepción del Audio:** Averiguar cómo gestionaba Zenonia 2 el audio (¿OpenSL ES?, ¿JNI de MediaPlayer de Android?).
- [ ] **Mapeo a libaudiout / SoLoud:** Traducir esas llamadas para reproducir los sonidos en formato nativo usando las librerías de Vita, o decodificar los formatos que traen (OGG, MP3, WAV).

## 8. Almacenamiento y Assets

- [x] **Rutas del Sistema de Archivos:** Interceptar las llamadas a `fopen`, `open`, `stat`, etc. para redirigir las lecturas de los archivos hacia `ux0:data/zenonia-2/assets/` en la Vita.
- [x] **Empaquetado y Permisos:** Probar que el juego cargue correctamente todo desde `ux0_data` sin fallos. El usuario debe copiar manualmente la carpeta `assets` a esa ruta en su consola.

## 9. Construcción y LiveArea

- [x] **Configuración LiveArea (`sce_sys/`):** Editar `param.sfo` y añadir las imágenes estáticas (`icon0.png`, `pic0.png`, `bg0.png`) con el formato correcto (PNG 8-bit indexado, sin metadata macos `._`) para evitar el error `0x8010113D`.
- [x] **Compilación:** Correr el build script o `make` para generar el `zenonia2.vpk` final.

## 10. Pruebas y Depuración

- [ ] **Consola Real (prioridad actual):** Desplegar `build/zenonia_2.vpk` + `ux0_data/zenonia-2/` por FTP (VitaShell) y monitorear `ux0:data/zenonia-2/log.txt` para corregir accesos ilegales a memoria (Data Abort) e iterar — metodología detallada en la skill `psvita-porting` (`references/hardware_debugging.md`). Si crashea, analizar el `.psp2dmp` con `vita-parse-core` contra `build/zenonia_2.elf`.
- [x] ~~Vita3K~~: descartado temporalmente como entorno de prueba — vitaGL crashea de forma reproducible al iniciar Vita3K, confirmado como inestabilidad del propio emulador (afecta también a un port de referencia que sí funciona) y no del código de este port. Ver `port_progress.md` §9.4 para el detalle de la investigación.
