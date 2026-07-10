# Plan de Port de Zenonia 2 para PS Vita

Este plan detalla los pasos necesarios para llevar a cabo el port de Zenonia 2 (Android) a PS Vita utilizando el `psvita-port-toolkit` (SoLoader Boilerplate) proporcionado, la librería `.so` del juego original y sus assets.

> **Actualización 2026-07-09 — leer antes de seguir:** El juego ha superado la etapa de inicio y ahora llega al bucle principal de renderizado sin crashear. Sin embargo, se está depurando un problema donde la pantalla se renderiza completamente en blanco. Ver `port_progress.md` Fase 10 para el detalle completo.

## 1. Configuración del Entorno y Preparación
- [x] **Configurar VitaSDK:** Asegurar que las variables de entorno de VitaSDK están correctamente configuradas y actualizadas.
- [x] **Dependencias:** Verificar que las librerías necesarias estén instaladas en el entorno (ej: `vitaGL`, `mathneon`, `kubridge`, `taihen`, etc.).
- [x] **Estructura del Proyecto:** Integrar el código base de `psvita-port-toolkit` dentro de la carpeta actual y asegurar que el `CMakeLists.txt` apunte correctamente a los archivos fuente en `loader/`.

## 2. Análisis del Ejecutable Original (`.so`)
- [x] **Inspección de la librería:** Funciones JNI y de renderizado identificadas mediante `nm`.
- [x] **Funciones importadas y exportadas:** Mapeado de OpenGL ES, libc y Android logs.

## 3. Carga Dinámica (SoLoader)
- [x] **Mapeo de Memoria:** Usar `so_file_load` dentro del loader en C para cargar la librería dinámica.
- [x] **Resolución de Dependencias:** Relocalización de funciones externas.
- [x] **Manejo de Librerías Falsas (Fake Libraries):** Emulación de libc y OpenGL.

## 4. Implementación de Falso JNI (Java Native Interface)
- [x] **Entorno Falso JNI:** Reemplazado por **FalsoJNI** desde Prince of Persia.
- [x] **Inicialización (NativeInit):** Confirmado y en funcionamiento.
- [x] **Ciclo de Vida:** Funciones de reanudación y eventos (ej: `NativeResumeClet`) llamadas correctamente.

## 5. Renderizado y Gráficos (vitaGL)
- [x] **Traducción OpenGL ES:** Enlazado a `vitaGL` y Wrappers creados (`glClearColorx`, `glTexImage2D`, `glDrawArrays`, etc).
- [x] **Configuración del Contexto:** `vglInitExtended` operativo sin MSAA.
- [ ] **Resolución de Pantalla Blanca:** Depuración actual. Se han corregido formatos de píxeles (`RGB565` a `RGBA8888`), filtros de texturas incompletas y el estado del cliente de vértices. Pendiente de verificación en consola real.

## 6. Entradas (Controles y Táctil)
- [x] **Mapeo de Botones:** `sceCtrlReadBufferPositive` implementado.
- [x] **Entrada Táctil:** Soporte táctil básico configurado.

## 7. Audio (Música y SFX)
- [ ] **Intercepción del Audio:** `[Java] OnSoundPlay` y referencias a `.mmf` identificadas.
- [ ] **Mapeo a libaudiout / SoLoud:** Traducir llamadas de JNI y decodificar los audios, probablemente utilizando como base el código de *Prince of Persia*.

## 8. Almacenamiento y Assets
- [x] **Rutas del Sistema de Archivos:** `fopen_hook`, `stat_hook`, y lecturas mediante JNI redirigidas correctamente a `ux0:data/zenonia-2/assets/`.
- [x] **Empaquetado y Permisos:** Assets cargando sin Data Aborts.

## 9. Construcción y LiveArea
- [x] **Configuración LiveArea (`sce_sys/`):** Imágenes generadas con paleta indexada y enlazadas en el build.
- [x] **Compilación:** `build.sh` completamente automatizado.

## 10. Pruebas y Depuración
- [x] **Consola Real (prioridad actual):** Iterando pruebas mediante VPKs generados para superar los bloqueos de renderizado.
