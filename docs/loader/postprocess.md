# `loader/postprocess.c` / `loader/postprocess.h` — Documentación de diseño

Comentarios explicativos extraídos del código fuente y reemplazados por bloques Doxygen técnicos en el código. Este documento conserva el razonamiento (el "por qué") separado de la documentación técnica.

## `VERT_SRC` / `FRAG_SRC` — por qué un shader de post-proceso (línea ~9)

**Archivo:** `loader/postprocess.c` — **Función/bloque:** `VERT_SRC`, `FRAG_SRC`

> Sharpen (unsharp mask, 4 vecinos) sobre el blit del compositor 400x240 ->
> pantalla completa. NO se toca el motor ni sus asset/buffers -- solo se
> reemplaza, para ese unico draw call por frame, el fixed-function texturing
> (GL_REPLACE) por este programa. Ver port_progress.md Backlog B.1 para el
> analisis de por que reemplazar assets no serviria (el motor compone todo a
> software en un buffer fijo 400x240) y por que un shader de post-proceso en
> el blit final es la unica via.
>
> v2 (2026-07-11): la v1 reusaba los vertex/texcoord arrays legacy que arma
> el motor (glVertexPointer/glTexCoordPointer) confiando en que vitaGL los
> "bridgea" automaticamente a los atributos POSITION/TEXCOORD0 de un shader
> custom. Resultado en consola: pantalla negra desde el menu en adelante. La
> hipotesis mas probable: el mapeo semantics->ATTR de Cg sigue la convencion
> estandar ARB_vertex_program (POSITION=ATTR0, pero TEXCOORD0=ATTR8, NO
> ATTR1), que no necesariamente coincide con el indice al que vitaGL bridgea
> el GL_TEXTURE_COORD_ARRAY legacy -- si no coincide, el shader lee texcoord
> basura/cero y muestrea siempre el mismo texel (podria leerse como "negro"
> si ese texel puntual es oscuro), aun con la textura y la geometria bien.
>
> v2 evita todo ese mapeo implicito: en vez de reusar los arrays del motor,
> dibuja SU PROPIO quad de pantalla completa con glVertexAttribPointer +
> glBindAttribLocation (API estandar, sin ambiguedad) usando geometria/UVs
> que YA CONOCEMOS son constantes (el compositor es siempre pantalla
> completa, con el sub-rect 400x240 fijo dentro de la textura POT -- ver
> pp_src_w/h). Esto reemplaza por completo, en vez de envolver, el
> glDrawArrays original del motor para ese unico draw call.

**Estado conocido:** ver `port_progress.md` Fase 14.1 — el build con este shader (`zenonia_2_shader.vpk`) produce pantalla negra desde el menú en adelante incluso con el rediseño v2; no tratar como alternativa funcional al build estándar sin antes revisar el log.

---

## `quad_uv` — orden de UVs confirmado por log (línea ~84)

**Archivo:** `loader/postprocess.c` — **Función/bloque:** `quad_uv` / `update_uv`

> UV correspondiente, recalculado cuando se conoce el tamaño real de la
> textura POT (postprocess_set_source_size). Orden confirmado contra el log
> de arranque (vertices/UVs del propio draw del motor): la fila 0 de la
> textura (V=0) corresponde a la parte de ARRIBA de la pantalla, V=240/alto
> a la de ABAJO.

---

## `postprocess_init` — binding explícito de atributos (línea ~132)

**Archivo:** `loader/postprocess.c` — **Función:** `postprocess_init`

> Bindear nosotros los indices de atributo ANTES de linkear, en vez de
> confiar en el mapeo automatico semantics->ATTR de Cg (ver nota v2
> arriba) -- alimentamos estos dos atributos nosotros mismos via
> glVertexAttribPointer, así que elegimos los indices.

---

## `postprocess_set_source_size` (línea ~14 de `postprocess.h`)

**Archivo:** `loader/postprocess.h` — **Función:** `postprocess_set_source_size`

> Informar el tamaño real (POT) de la textura del compositor, para el uniform
> de texel size. Llamar desde glTexImage2D_wrapper cuando se detecte la
> textura del compositor (RGB565, la unica que este motor sube via
> glTexImage2D/glTexSubImage2D).

---

## `postprocess_mark_next_draw` (línea ~20 de `postprocess.h`)

**Archivo:** `loader/postprocess.h` — **Función:** `postprocess_mark_next_draw`

> Marcar que el proximo glDrawArrays es el blit del compositor. Llamar desde
> glTexSubImage2D_wrapper cuando w==400 && h==240 (firma unica del compositor,
> confirmada en log -- ver port_progress.md Backlog B.1).

---

## `postprocess_try_draw` — contrato de coordinación con el wrapper (línea ~25 de `postprocess.h`)

**Archivo:** `loader/postprocess.h` — **Función:** `postprocess_try_draw`

> Si el draw fue marcado (postprocess_mark_next_draw) y el shader esta
> disponible, dibuja ELLA MISMA el quad de pantalla completa con su propia
> geometria/atributos (no reusa los vertex/texcoord arrays legacy del motor,
> ver postprocess.c) y devuelve 1 -- en ese caso, glDrawArrays_wrapper debe
> SALTEARSE su propio glDrawArrays para esta llamada (mismo resultado visual
> + el shader). Devuelve 0 (no hace nada) si el shader esta OFF en este
> build o no aplica a este draw -- glDrawArrays_wrapper debe llamar al
> glDrawArrays real como siempre en ese caso.

---
