#!/bin/bash

echo "Buscando y eliminando archivos ._ en el directorio actual y subdirectorios..."
find . -type f -name "._*" -delete
echo "¡Limpieza completada!"
