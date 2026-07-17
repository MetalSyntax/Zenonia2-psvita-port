# Guía de Instalación: Zenonia 2 (PS Vita Port)

Esta guía explica detalladamente cómo instalar el port de Zenonia 2, tanto en una consola PS Vita real como en el emulador Vita3K.

El juego requiere **dos componentes principales** para funcionar correctamente:
1. El instalador ejecutable (`build/zenonia_2.vpk`).
2. Los datos y recursos gráficos extraídos (`ux0_data/zenonia-2/`), que incluyen `drawable/`
   (logo.png/title.png/touch.png de la pantalla de splash/título, ver `loader/main.c`) además de
   `assets/` y `sound/`. Ya no van embebidos en el VPK: si copiás todo el contenido de `ux0_data/`
   como se indica abajo quedan incluidos automáticamente; `manage_vita.py` (opción 5) también permite
   subir solo esos tres PNG por FTP sin tocar el resto de los assets.

---

## Opción 1: Instalar en Hardware Real (PS Vita)

### Requisitos
- Una consola PS Vita con Henkaku/Enso instalado.
- La aplicación **VitaShell** instalada.
- El plugin **kubridge** y **fdfix** instalados en `ur0:tai/config.txt`.
- `libshacccg.suprx` extraído en `ur0:data/` (necesario para vitaGL).

### Pasos de Instalación
1. **Transferir el Instalador:**
   - Abre **VitaShell** en tu PS Vita y presiona `SELECT` para activar la conexión FTP o USB hacia tu computadora.
   - Pasa el archivo `build/zenonia_2.vpk` a cualquier ubicación de la consola (ej. `ux0:VPKs/`).
   
2. **Transferir los Datos (Assets):**
   - Desde tu computadora, arrastra **todo el contenido** de la carpeta `ux0_data/` de este proyecto hacia la raíz de tu tarjeta de memoria en la PS Vita (la partición `ux0:`).
   - Deberías terminar con la siguiente estructura: `ux0:data/zenonia-2/assets/` y dentro todos los sprites, música y mapas del juego.

3. **Instalación Final:**
   - En **VitaShell**, navega hasta donde dejaste el archivo `zenonia_2.vpk`.
   - Presiona `X` sobre él para instalarlo. Acepta los permisos de instalación adicionales que el Port requiera.
   - Una vez finalizada la instalación, cierra VitaShell.
   - ¡Verás la burbuja de Zenonia 2 en tu pantalla principal (LiveArea)! Toca la burbuja y disfruta del juego.

---

## Opción 2: Instalar en Emulador (Vita3K)

Vita3K simplifica bastante el proceso al emular los módulos, pero los archivos de datos deben inyectarse en su disco virtual de forma manual.

### Pasos de Instalación
1. **Instalar el ejecutable (.VPK):**
   - Abre la aplicación Vita3K en tu Mac (o Windows/Linux).
   - En la barra de menú superior, selecciona `File` > `Install .vpk`.
   - Busca en tu computadora el archivo `build/zenonia_2.vpk` y selecciónalo. El emulador lo instalará y aparecerá el icono del juego.

2. **Inyectar los Datos (Assets):**
   - A diferencia de un APK de Android, los emuladores de Vita necesitan que la carpeta de assets se ponga a mano en el disco duro virtual.
   - En macOS, abre el buscador (Finder) y presiona `Cmd + Shift + G`, luego ve a la siguiente ruta de Vita3K:
     `~/.local/share/Vita3K/ux0/data/`
   - *(En Windows, esta ruta suele ser `C:\Users\TU_USUARIO\AppData\Roaming\Vita3K\ux0\data\`)*
   - Copia la carpeta `zenonia-2` de este proyecto, de manera que quede estructurada como: `ux0/data/zenonia-2/assets/...`

3. **Ejecución Final:**
   - Haz doble clic sobre el icono de Zenonia 2 en la pantalla de Vita3K.
   - Si los gráficos no se muestran correctamente, ve a los ajustes de configuración (Configuration -> Settings -> GPU) y asegúrate de forzar el backend a **OpenGL**.
