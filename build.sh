#!/bin/bash
set -e

# Configuración
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="/tmp/zenonia2-build"
SRC_DIR="/tmp/zenonia2-src"
VPK_NAME="zenonia_2.vpk"

echo "================================================================"
echo "  🚀 Script de Build Automático para Zenonia 2 (PS Vita)"
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

cmake "$SRC_DIR" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)

echo "[3/4] Exportando archivos generados..."
mkdir -p "$PROJECT_DIR/build"
cp "$VPK_NAME" "$PROJECT_DIR/build/$VPK_NAME"
if [ -f "eboot.bin" ]; then
    cp "eboot.bin" "$PROJECT_DIR/build/eboot.bin"
fi
if [ -f "zenonia_2" ]; then
    cp "zenonia_2" "$PROJECT_DIR/build/zenonia_2.elf"
fi

echo "✅ Build exitoso: $PROJECT_DIR/build/$VPK_NAME"

echo "[4/4] Instalación..."
VITA3K_APP="/Applications/Vita3K.app/Contents/MacOS/Vita3K"
if [ -x "$VITA3K_APP" ]; then
    echo "🎮 Lanzando Vita3K y cargando VPK..."
    # Vita3K instala y ejecuta automáticamente el VPK cuando se le pasa por argumento
    # Forzamos el backend a OpenGL
    "$VITA3K_APP" -B OpenGL "$PROJECT_DIR/build/$VPK_NAME" > /dev/null 2>&1 &
    echo "¡Listo! El juego se está abriendo."
else
    echo "⚠️ Vita3K no encontrado en la ruta por defecto (/Applications/Vita3K.app)."
    echo "=== INSTRUCCIONES PARA VITA3K ==="
    echo "1. Ve a 'File -> Install .vpk' y selecciona 'build/zenonia_2.vpk'."
    echo "2. Copia todo el contenido de 'ux0_data/zenonia-2/' a la ruta virtual del emulador:"
    echo "   (Normalmente en: ~/.local/share/Vita3K/ux0/data/zenonia-2/)"
fi
