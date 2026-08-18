# `loader/image_load.c` / `loader/image_load.h` — Documentación de diseño

Comentarios explicativos extraídos del código fuente y reemplazados por bloques Doxygen técnicos en el código. Este documento conserva el razonamiento (el "por qué") separado de la documentación técnica.

## `image_load_png_tex` (línea ~17, `loader/image_load.h`)

**Archivo:** `loader/image_load.h` — **Función:** `image_load_png_tex`

> `> 0.0f`   -> use this scale factor verbatim instead of deriving one from
> dst_w/dst_h (used for touch.png, which historically reused
> title's cover factor rather than its own aspect ratio).

---
