#!/bin/bash
set -e

echo "================================================================"
echo "  🚀 Script de Build para Beta Testers (Zenonia 2)"
echo "================================================================"

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
VPK_NORMAL="zenonia_2.vpk"
VPK_TESTER="zenonia_2_tester.vpk"

# Ejecutar el build normal saltando las preguntas interactivas
echo "Compilando build normal..."
echo -e "\n\n" | ./build.sh normal

echo "Empaquetando datos de ux0_data dentro del VPK..."
cd "$PROJECT_DIR/build"
cp "$VPK_NORMAL" "$VPK_TESTER"

# Preparar la estructura data/zenonia-2 que VitaShell instalará en ux0:data/
TMP_DIR=$(mktemp -d)
mkdir -p "$TMP_DIR/data"
cp -r "$PROJECT_DIR/ux0_data/zenonia-2" "$TMP_DIR/data/"

# Añadir la carpeta data al VPK (que es un zip)
cd "$TMP_DIR"
zip -ur "$PROJECT_DIR/build/$VPK_TESTER" data/zenonia-2

# Limpieza
rm -rf "$TMP_DIR"

echo "✅ Build para tester creado exitosamente: $PROJECT_DIR/build/$VPK_TESTER"
echo "El VPK contiene la carpeta data/zenonia-2 que será extraida por VitaShell a ux0:data/zenonia-2"
