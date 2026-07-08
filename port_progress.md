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

## Fase 4: Implementación de Falso JNI (FakeJNI) (Completada)
- [x] **Enlazado de Símbolos:** Se configuraron punteros a funciones en `main.c` para extraer dinámicamente `JNI_OnLoad`, `NativeInit`, `NativeRender` y `NativeResize` usando la macro `so_symbol`.
- [x] **Secuencia de Arranque (Android Lifecycle):** Se crearon punteros ficticios para el Entorno Java (`fake_vm` y `fake_env`) y se enviaron como parámetros para sortear los controles del juego y ejecutar el constructor gráfico `NativeInit`.
- [x] **vitaGL & Main Loop:** Se inicializó el contexto de OpenGL (`vglInitExtended`) con la resolución nativa de Vita (960x544) y se construyó el bucle infinito (`while(1)`) que ejecuta `NativeRender` por cada fotograma e intercambia los buffers (`vglSwapBuffers`). Se añadió una salida de emergencia presionando el botón `START`.

## Fase 5: Mapeo de Controles y Entrada Táctil (Completada)
- [x] **Detección de Exportaciones de Entrada:** Al inspeccionar los símbolos, se encontró `Java_com_gamevil_nexus2_Natives_setInputEvent`, la función que el framework de Gamevil usa para inyectar comandos de Android (Touches/Keys).
- [x] **Lectura Táctil de Vita:** Se habilitó el muestreo de la pantalla táctil capacitiva con `sceTouchSetSamplingState` y la lectura por frame en `sceTouchPeek`.
- [x] **Normalización y Conversión:** Las coordenadas en bruto del hardware táctil de Vita (1920x1088) se normalizan a la resolución del renderizador (960x544). Además, se programó un sistema básico de estado ("Touch Down", "Touch Move", "Touch Up") que inyecta el tipo de evento correspondiente hacia la máquina falsa de Java.

## Fase 6: Extraer Assets y Empaquetar a VPK (En Progreso)
- [x] **Descomprimir APK:** Se ha extraído exitosamente el archivo `zenonia2-1-0-5.zip` en la carpeta temporal `apk_extract/`.
- [x] **Migración de Assets:** Se ha iniciado la transferencia masiva de la carpeta `assets/` extraída hacia el directorio final `ux0_data/zenonia-2/assets/` donde la librería `.so` buscará todos sus recursos.
- [x] **Empaquetado de LiveArea (sce_sys):** Se extrajo el icono de alta resolución (`icon170.png`) original y se ubicó en `sce_sys/icon0.png`, `pic0.png` y `startup.png`. Además se generó el `template.xml` básico. 
*(Nota Técnica: Para compilar con éxito el .vpk final, asegúrate de convertir estos PNGs a formato 8-bit indexado usando Photoshop, GIMP o ImageMagick, tal como exige la PS Vita).*

## Fase 7: Implementación para Emulador (Vita3K) y Pruebas
- [x] **Automatización de Build:** Se reconstruyó el archivo `build.sh` basándose en una estrategia segura que copia el código fuente a `/tmp` antes de compilar para evitar el temido error de espacios en los directorios al empaquetar VPKs.
- [x] **Despliegue a Vita3K:** Se generó una copia del instalable nombrada exclusivamente como `zenonia_2_vita3k.vpk` y se inyectaron automáticamente los datos gráficos del juego en la carpeta del disco duro virtual del emulador.

## Siguiente Paso del Plan de Porteo Original
Basado en `plan_zenonia_port.md`, **hemos completado hasta el apartado 6 (Entradas y Táctil) y saltado al 8-10 para empaquetar.** Nos queda pendiente la **Fase 7: Audio (Música y SFX)**. Deberemos interceptar las llamadas JNI del MediaPlayer original de Android para traducirlas a las librerías de sonido nativas de la Vita (o simplemente silenciarlas de forma segura para evitar crasheos si el juego intenta reproducir música).
