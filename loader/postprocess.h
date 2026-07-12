#ifndef POSTPROCESS_H
#define POSTPROCESS_H

// Shader de post-proceso (sharpen) opcional para el blit del compositor 400x240
// -> pantalla completa (ver port_progress.md Backlog B.1). Sin cuerpo real a
// menos que se compile con -DPOSTPROCESS_SHADER (CMake option
// ENABLE_POSTPROCESS_SHADER, OFF por defecto) -- los call sites en main.c/
// dynlib.c son incondicionales, así que esta capa queda 100% neutra (no-op)
// en el build default/probado en consola.

// Compilar y linkear el programa de shaders. Llamar una vez despues de gl_init().
void postprocess_init(void);

// Informar el tamaño real (POT) de la textura del compositor, para el uniform
// de texel size. Llamar desde glTexImage2D_wrapper cuando se detecte la
// textura del compositor (RGB565, la unica que este motor sube via
// glTexImage2D/glTexSubImage2D).
void postprocess_set_source_size(int tex_w, int tex_h);

// Marcar que el proximo glDrawArrays es el blit del compositor. Llamar desde
// glTexSubImage2D_wrapper cuando w==400 && h==240 (firma unica del compositor,
// confirmada en log -- ver port_progress.md Backlog B.1).
void postprocess_mark_next_draw(void);

// Si el draw fue marcado (postprocess_mark_next_draw) y el shader esta
// disponible, dibuja ELLA MISMA el quad de pantalla completa con su propia
// geometria/atributos (no reusa los vertex/texcoord arrays legacy del motor,
// ver postprocess.c) y devuelve 1 -- en ese caso, glDrawArrays_wrapper debe
// SALTEARSE su propio glDrawArrays para esta llamada (mismo resultado visual
// + el shader). Devuelve 0 (no hace nada) si el shader esta OFF en este
// build o no aplica a este draw -- glDrawArrays_wrapper debe llamar al
// glDrawArrays real como siempre en ese caso.
int postprocess_try_draw(void);

#endif
