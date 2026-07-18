#!/bin/bash
set -e

# Uso: ./build.sh [normal|shader|safe|rgb565test]
# "shader" compila con ENABLE_POSTPROCESS_SHADER=ON (ver port_progress.md
# Backlog B.1: sharpen de post-proceso sobre el blit del compositor) y genera
# zenonia_2_shader.vpk en un build dir separado -- no toca el build "normal"
# ya confirmado en consola (build/zenonia_2.vpk).
# "safe" compila un VPK sin assets con copyright, seguro para distribución.
# "rgb565test" compila con ENABLE_NATIVE_RGB565_TEST=ON (ver port_progress.md
# Fase 18: experimento para confirmar si vitaGL sigue rechazando RGB565 nativo
# con GL_INVALID_ENUM, saltando la conversion a RGBA8888 en CPU) y genera
# zenonia_2_rgb565test.vpk, tambien en build dir separado.
VARIANT="${1:-normal}"

# Configuración
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="/tmp/zenonia2-src"
case "$VARIANT" in
    normal)
        BUILD_DIR="/tmp/zenonia2-build"
        CMAKE_POSTPROCESS_FLAG="-DENABLE_POSTPROCESS_SHADER=OFF -DSAFE_DISTRIBUTION=OFF"
        VPK_NAME="zenonia_2.vpk"
        ;;
    shader)
        BUILD_DIR="/tmp/zenonia2-build-shader"
        CMAKE_POSTPROCESS_FLAG="-DENABLE_POSTPROCESS_SHADER=ON -DSAFE_DISTRIBUTION=OFF"
        VPK_NAME="zenonia_2_shader.vpk"
        ;;
    safe)
        BUILD_DIR="/tmp/zenonia2-build-safe"
        CMAKE_POSTPROCESS_FLAG="-DENABLE_POSTPROCESS_SHADER=OFF -DSAFE_DISTRIBUTION=ON"
        VPK_NAME="zenonia_2_safe.vpk"
        ;;
    rgb565test)
        BUILD_DIR="/tmp/zenonia2-build-rgb565test"
        CMAKE_POSTPROCESS_FLAG="-DENABLE_POSTPROCESS_SHADER=OFF -DSAFE_DISTRIBUTION=OFF -DENABLE_NATIVE_RGB565_TEST=ON"
        VPK_NAME="zenonia_2_rgb565test.vpk"
        ;;
    *)
        echo "❌ Variante desconocida: '$VARIANT' (usar 'normal', 'shader', 'safe' o 'rgb565test')"
        exit 1
        ;;
esac

echo "================================================================"
echo "  🚀 Script de Build Automático para Zenonia 2 (PS Vita) [$VARIANT]"
echo "================================================================"

echo "[1/4] Preparando entorno de compilación..."
# Evitamos problemas de rutas con espacios usando un directorio temporal en /tmp
mkdir -p "$BUILD_DIR"
mkdir -p "$SRC_DIR"

# Asegurar que VITASDK está definido
if [ -z "$VITASDK" ]; then
    if [ -d "/usr/local/vitasdk" ]; then
        export VITASDK="/usr/local/vitasdk"
    elif [ -d "$HOME/vitasdk" ]; then
        export VITASDK="$HOME/vitasdk"
    else
        echo "❌ Error: La variable de entorno VITASDK no está definida y no se encontró en rutas por defecto."
        exit 1
    fi
    export PATH="$VITASDK/bin:$PATH"
fi

# Sincronizamos el código fuente (excluyendo el historial y builds viejos)
rsync -a --exclude '.git' --exclude 'build' --exclude '.*' "$PROJECT_DIR/" "$SRC_DIR/"

echo "[2/4] Ejecutando CMake y Make..."
cd "$BUILD_DIR"

cmake "$SRC_DIR" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release $CMAKE_POSTPROCESS_FLAG
make -j$(sysctl -n hw.ncpu)

echo "[3/4] Exportando archivos generados..."
mkdir -p "$PROJECT_DIR/build"
cp "$VPK_NAME" "$PROJECT_DIR/build/$VPK_NAME"
if [ -f "eboot.bin" ]; then
    cp "eboot.bin" "$PROJECT_DIR/build/eboot.bin"
fi
if [ -f "zenonia_2" ]; then
    cp "zenonia_2" "$PROJECT_DIR/build/${VPK_NAME%.vpk}.elf"
fi

echo "✅ Build exitoso: $PROJECT_DIR/build/$VPK_NAME"

echo "¿Para qué plataforma quieres configurar la instalación? (1: Vita3K, 2: PS Vita)"
read -p "Opción [1]: " PLATFORM_OPTION
PLATFORM_OPTION=${PLATFORM_OPTION:-1}

read -p "¿Quieres instalar o transferir el .vpk automáticamente? (s/n) [s]: " INSTALL_VPK
INSTALL_VPK=${INSTALL_VPK:-s}

echo "[4/4] Instalación..."
if [ "$INSTALL_VPK" = "s" ] || [ "$INSTALL_VPK" = "S" ]; then
    if [ "$PLATFORM_OPTION" = "1" ]; then
        VITA3K_APP="/Applications/Vita3K.app/Contents/MacOS/Vita3K"
        if [ -x "$VITA3K_APP" ]; then
            echo "🎮 Lanzando Vita3K y cargando VPK..."
            # Forzamos el backend a OpenGL
            "$VITA3K_APP" -B OpenGL "$PROJECT_DIR/build/$VPK_NAME" > /dev/null 2>&1 &
            echo "¡Listo! El juego se está abriendo."
        else
            echo "⚠️ Vita3K no encontrado en la ruta por defecto (/Applications/Vita3K.app)."
            echo "=== INSTRUCCIONES PARA VITA3K ==="
            echo "1. Ve a 'File -> Install .vpk' y selecciona 'build/$VPK_NAME'."
            echo "2. Copia todo el contenido de 'ux0_data/zenonia-2/' a la ruta virtual del emulador:"
            echo "   (Normalmente en: ~/.local/share/Vita3K/ux0/data/zenonia-2/)"
        fi
    elif [ "$PLATFORM_OPTION" = "2" ]; then
        echo "Asegúrate de que VitaShell esté abierto con el servidor FTP activado en tu PS Vita."
        read -p "Ingresa la dirección IP de tu PS Vita (ej: 192.168.1.100) (Deja en blanco para cancelar): " VITA_IP
        if [ ! -z "$VITA_IP" ]; then
            echo "Enviando VPK a ftp://$VITA_IP:1337/ux0:/vpk/$VPK_NAME ..."
            curl -T "$PROJECT_DIR/build/$VPK_NAME" "ftp://$VITA_IP:1337/ux0:/vpk/$VPK_NAME"
            echo "✅ Archivo transferido. Recuerda instalar el .vpk desde VitaShell en ux0:/vpk/ y copiar tus archivos de datos a ux0:/data/zenonia-2/"
        else
            echo "Transferencia a PS Vita cancelada."
        fi
    else
        echo "Opción no válida. Instalación cancelada."
    fi
else
    echo "Instalación omitida por el usuario."
fi
