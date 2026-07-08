# Plan de Port de Zenonia 2 para PS Vita

Este plan detalla los pasos necesarios para llevar a cabo el port de Zenonia 2 (Android) a PS Vita utilizando el `psvita-port-toolkit` (SoLoader Boilerplate) proporcionado, la librería `.so` del juego original y sus assets.

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

- [ ] **Entorno Falso JNI:** El juego de Android usa JNI para interactuar con la parte Java (actividad principal, inputs, música, guardado). Implementar una estructura JNI falsa para atrapar las llamadas.
- [ ] **Inicialización (NativeInit):** Enganchar e invocar el método equivalente a `JNI_OnLoad` o las funciones nativas que levantan la aplicación.
- [ ] **Ciclo de Vida:** Emular llamadas al ciclo de vida de la actividad (pausa, reanudación, destrucción).

## 5. Renderizado y Gráficos (vitaGL)

- [ ] **Traducción OpenGL ES:** Enlazar las llamadas de GLES 1.1 / 2.0 que hace la librería a las implementaciones proporcionadas por `vitaGL` o parchearlas directamente en la Vita.
- [ ] **Configuración del Contexto EGL/GLES:** Crear una ventana (framebuffer) compatible con la Vita y pasarle el contexto al hilo principal de renderizado del juego.
- [ ] **Resolución de Pantalla:** Escalar o ajustar la resolución original de Zenonia 2 (posiblemente para pantallas de teléfonos de la época) a la resolución nativa de la Vita (960x544).

## 6. Entradas (Controles y Táctil)

- [ ] **Mapeo de Botones:** Leer la entrada física usando `sceCtrlReadBufferPositive`. Enviar estos eventos a la lógica del juego. Como Zenonia 2 era originalmente táctil pero con un pad virtual, puede requerir inyectar coordenadas táctiles o interceptar el mapeo del D-pad virtual hacia botones reales de la Vita.
- [ ] **Entrada Táctil:** Habilitar el uso de `sceTouchRead` para soportar la pantalla táctil en menús, inyectando eventos equivalentes de "Touch Down", "Touch Move" y "Touch Up" vía el Falso JNI.

## 7. Audio (Música y SFX)

- [ ] **Intercepción del Audio:** Averiguar cómo gestionaba Zenonia 2 el audio (¿OpenSL ES?, ¿JNI de MediaPlayer de Android?).
- [ ] **Mapeo a libaudiout / SoLoud:** Traducir esas llamadas para reproducir los sonidos en formato nativo usando las librerías de Vita, o decodificar los formatos que traen (OGG, MP3, WAV).

## 8. Almacenamiento y Assets

- [ ] **Rutas del Sistema de Archivos:** Interceptar las llamadas a `fopen`, `open`, `stat`, etc. para redirigir las lecturas de los archivos hacia `ux0:data/zenonia-2/assets/` en la Vita.
- [ ] **Empaquetado y Permisos:** Probar que el juego cargue correctamente todo desde `ux0_data` sin fallos. El usuario debe copiar manualmente la carpeta `assets` a esa ruta en su consola.

## 9. Construcción y LiveArea

- [ ] **Configuración LiveArea (`sce_sys/`):** Editar `param.sfo` y añadir las imágenes estáticas (`icon0.png`, `pic0.png`, `bg0.png`) con el formato correcto (PNG 8-bit indexado, sin metadata macos `._`) para evitar el error `0x8010113D`.
- [ ] **Compilación:** Correr el build script o `make` para generar el `zenonia2.vpk` final.

## 10. Pruebas y Depuración

- [ ] **Vita3K / Consola Real:** Desplegar el `.vpk` por FTP y monitorear la salida en la consola de depuración para corregir accesos ilegales a memoria (Data Abort) e iterar.
