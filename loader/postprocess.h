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

/**
 * @brief Registers the compositor texture's real POT dimensions for the shader's texel-size uniform.
 * @param tex_w POT texture width.
 * @param tex_h POT texture height.
 * @note Ver docs/loader/postprocess.md para el razonamiento de diseño.
 */
void postprocess_set_source_size(int tex_w, int tex_h);

/**
 * @brief Flags the next glDrawArrays call as the compositor's full-frame blit.
 * @note Ver docs/loader/postprocess.md para el razonamiento de diseño.
 */
void postprocess_mark_next_draw(void);

/**
 * @brief Draws the post-process quad itself if the last draw was flagged and the shader is available.
 * @return Non-zero if it drew the quad (caller must skip its own glDrawArrays for this call);
 *         0 if the shader is off or this draw wasn't flagged (caller must draw as usual).
 * @note Ver docs/loader/postprocess.md para el razonamiento de diseño.
 */
int postprocess_try_draw(void);

#endif
