/**
 * @file controls.h
 * @brief Remappable physical controls + in-game "PS Vita controls" menu
 *        (Start+Select), same UX as the Carnivores Vita port.
 */
#ifndef ZEN2_CONTROLS_H
#define ZEN2_CONTROLS_H

#include <stdint.h>
#include <psp2/ctrl.h>
#include <vitaGL.h>

/** Game actions, each sent to the engine as one HAL keycode. */
enum {
  ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
  ACT_OK, ACT_BACK, ACT_SKIP, ACT_MAP, ACT_SAVE,
  ACT_COUNT
};

extern volatile int g_vbuttons_visible; /**< touch D-pad/buttons shown (L+R) */
extern int g_fps_original; /**< 0 = 30 FPS cap, 1 = engine's own speed (Select+R) */

void controls_init(void);                  /**< load controls.txt or defaults */
void controls_save(void);                  /**< write controls.txt */
uint32_t controls_read(const SceCtrlData *pad); /**< buttons + stick + rear touch */
uint32_t controls_binding(int action);     /**< SCE_CTRL_* mask bound to action */
int controls_action_hal(int action);       /**< HAL keycode of action */
/** data/ subdir replacing data/eng/ ("spa"/"por"), NULL = English. */
const char *controls_lang_dir(void);

int controls_menu_active(void);
void controls_menu_open(void);
/** Feeds this frame's buttons to the open menu; returns 1 the frame it closes. */
int controls_menu_update(uint32_t held, uint32_t pressed);
/** Texture with the menu rendered (480x272, draw fullscreen). */
GLuint controls_menu_texture(void);

#endif
