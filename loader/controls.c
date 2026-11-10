/**
 * @file controls.c
 * @brief Remappable controls + "PS Vita controls" menu (Start+Select).
 *
 * Each game action (HAL keycode, see main.c) is bound to a set of Vita
 * buttons; any of them held = action held. Rear touch quadrants count as
 * buttons (L2/R2/L3/R3 bits, unused by the Vita pad). Bindings persist in
 * ux0:data/zenonia-2/controls.txt, hand-editable ("ACTION = BTN, BTN").
 * The menu is drawn with debugScreen's 8x8 font into a 480x272 texture.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <psp2/touch.h>

#include "controls.h"
#include "debugScreen.h"

extern void game_log(const char *fmt, ...);
extern PsvDebugScreenFont psvDebugScreenFont;
extern void set_virtual_buttons_visible(int visible); // main.c
extern void set_fps_mode(int original);               // main.c
extern void set_text_language(const char *old, const char *dir); // main.c
extern volatile int g_engine_fps;                     // java.c

#define CONTROLS_PATH "ux0:data/zenonia-2/controls.txt"

// Rear touch quadrants reuse pad bits sceCtrlPeekBufferPositive never sets on
// a Vita (SCE_CTRL_L2/R2 alias the L/R triggers, so they can't be used).
#define REAR_TL SCE_CTRL_L1
#define REAR_TR SCE_CTRL_R1
#define REAR_BL SCE_CTRL_L3
#define REAR_BR SCE_CTRL_R3

static const struct {
  const char *name;
  uint32_t bit;
} btn_names[] = {
    {"CROSS", SCE_CTRL_CROSS},   {"CIRCLE", SCE_CTRL_CIRCLE},
    {"SQUARE", SCE_CTRL_SQUARE}, {"TRIANGLE", SCE_CTRL_TRIANGLE},
    {"L1", SCE_CTRL_LTRIGGER},   {"R1", SCE_CTRL_RTRIGGER},
    {"UP", SCE_CTRL_UP},         {"DOWN", SCE_CTRL_DOWN},
    {"LEFT", SCE_CTRL_LEFT},     {"RIGHT", SCE_CTRL_RIGHT},
    {"L2", REAR_TL},             {"R2", REAR_TR},
    {"L3", REAR_BL},             {"R3", REAR_BR},
};
#define BTN_NAME_COUNT (int)(sizeof(btn_names) / sizeof(btn_names[0]))
#define BINDABLE_MASK                                                          \
  (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE |   \
   SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_UP | SCE_CTRL_DOWN |      \
   SCE_CTRL_LEFT | SCE_CTRL_RIGHT | REAR_TL | REAR_TR | REAR_BL | REAR_BR)

static const struct {
  const char *key;   // controls.txt name
  const char *label; // menu label
  int hal;           // HAL keycode (Zenonia2UIControllerView.getHalKeyCode)
  uint32_t def;      // default binding
} actions[ACT_COUNT] = {
    {"UP", "Move up", -1, SCE_CTRL_UP},
    {"DOWN", "Move down", -2, SCE_CTRL_DOWN},
    {"LEFT", "Move left", -3, SCE_CTRL_LEFT},
    {"RIGHT", "Move right", -4, SCE_CTRL_RIGHT},
    {"OK", "Attack / OK", -5, SCE_CTRL_CROSS},
    {"BACK", "Menu / Back", -16, SCE_CTRL_CIRCLE},
    {"SKIP", "Quick slot / Skip", 35, SCE_CTRL_TRIANGLE},
    {"MAP", "Map", -6, SCE_CTRL_SQUARE},
    {"SAVE", "Save", -10, SCE_CTRL_LTRIGGER},
};

static uint32_t binding[ACT_COUNT];
volatile int g_vbuttons_visible = 0;
int g_fps_original = 0;

// Extra text languages: data/eng/*.zt1 is served from data/<dir>/ (see java.c).
static const struct { const char *key, *label, *dir; } langs[] = {
    {"EN", "ENGLISH", NULL}, {"ES", "ESPANOL", "spa"}, {"PT", "PORTUGUES", "por"},
};
#define LANG_COUNT (int)(sizeof(langs) / sizeof(langs[0]))
static int lang = 0;
const char *controls_lang_dir(void) { return langs[lang].dir; }

uint32_t controls_binding(int a) { return binding[a]; }
int controls_action_hal(int a) { return actions[a].hal; }

static void set_defaults(void) {
  for (int a = 0; a < ACT_COUNT; a++)
    binding[a] = actions[a].def;
}

void controls_save(void) {
  FILE *f = fopen(CONTROLS_PATH, "w");
  if (!f) {
    game_log("[CONTROLS] no se pudo escribir %s\n", CONTROLS_PATH);
    return;
  }
  for (int a = 0; a < ACT_COUNT; a++) {
    fprintf(f, "%s =", actions[a].key);
    int any = 0;
    for (int b = 0; b < BTN_NAME_COUNT; b++)
      if (binding[a] & btn_names[b].bit)
        fprintf(f, "%s %s", any++ ? "," : "", btn_names[b].name);
    fprintf(f, "%s\n", any ? "" : " NONE");
  }
  fprintf(f, "FPS = %s\n", g_fps_original ? "ORIGINAL" : "30");
  fprintf(f, "LANG = %s\n", langs[lang].key);
  fclose(f);
}

static char *trim(char *s) {
  while (isspace((unsigned char)*s)) s++;
  char *e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
  return s;
}

void controls_init(void) {
  set_defaults();
  FILE *f = fopen(CONTROLS_PATH, "r");
  if (!f) {
    controls_save(); // write the defaults so the file can be hand-edited
    return;
  }
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char *key = trim(line);
    if (!strcasecmp(key, "FPS")) {
      g_fps_original = !strcasecmp(trim(eq + 1), "ORIGINAL");
      continue;
    }
    if (!strcasecmp(key, "LANG")) {
      for (int l = 0; l < LANG_COUNT; l++)
        if (!strcasecmp(trim(eq + 1), langs[l].key)) lang = l;
      continue;
    }
    int a;
    for (a = 0; a < ACT_COUNT && strcasecmp(key, actions[a].key); a++);
    if (a == ACT_COUNT) continue; // unknown action: ignored
    binding[a] = 0;
    for (char *tok = strtok(eq + 1, ","); tok; tok = strtok(NULL, ",")) {
      tok = trim(tok);
      for (int b = 0; b < BTN_NAME_COUNT; b++)
        if (!strcasecmp(tok, btn_names[b].name)) binding[a] |= btn_names[b].bit;
    }
  }
  fclose(f);
  game_log("[CONTROLS] %s cargado\n", CONTROLS_PATH);
  set_fps_mode(g_fps_original);
}

uint32_t controls_read(const SceCtrlData *pad) {
  uint32_t b = pad->buttons;
  // Left stick -> D-pad bits (deadzone: half tilt)
  if (pad->lx < 64) b |= SCE_CTRL_LEFT;
  if (pad->lx > 192) b |= SCE_CTRL_RIGHT;
  if (pad->ly < 64) b |= SCE_CTRL_UP;
  if (pad->ly > 192) b |= SCE_CTRL_DOWN;

  SceTouchData rear;
  if (sceTouchPeek(SCE_TOUCH_PORT_BACK, &rear, 1) > 0) {
    for (int i = 0; i < rear.reportNum; i++) {
      int left = rear.report[i].x < 960;
      // ponytail: rear panel y spans ~108..889, midpoint hardcoded; use
      // sceTouchGetPanelInfo if quadrants feel off on hardware.
      int top = rear.report[i].y < 500;
      b |= top ? (left ? REAR_TL : REAR_TR) : (left ? REAR_BL : REAR_BR);
    }
  }
  return b;
}

/* ------------------------------ menu ------------------------------ */

enum { ROW_VBUTTONS = ACT_COUNT, ROW_FPS, ROW_LANG, ROW_DEFAULTS, ROW_COUNT };
enum { CAP_NONE, CAP_REPLACE, CAP_ADD };

static int menu_open = 0, cursor = 0, capture = CAP_NONE, dirty = 1;

int controls_menu_active(void) { return menu_open; }

void controls_menu_open(void) {
  menu_open = 1;
  capture = CAP_NONE;
  dirty = 1;
}

int controls_menu_update(uint32_t held, uint32_t pressed) {
  (void)held;
  if (!menu_open || !pressed) return 0;
  dirty = 1;

  if (capture != CAP_NONE) {
    if (pressed & SCE_CTRL_START) { // cancel
      capture = CAP_NONE;
      return 0;
    }
    uint32_t bit = pressed & BINDABLE_MASK;
    if (!bit) return 0;
    bit &= -bit; // one button per press
    for (int a = 0; a < ACT_COUNT; a++)
      binding[a] &= ~bit; // a button belongs to one action only
    binding[cursor] = (capture == CAP_ADD ? binding[cursor] : 0) | bit;
    capture = CAP_NONE;
    return 0;
  }

  if (pressed & SCE_CTRL_UP) cursor = (cursor + ROW_COUNT - 1) % ROW_COUNT;
  if (pressed & SCE_CTRL_DOWN) cursor = (cursor + 1) % ROW_COUNT;
  if (pressed & SCE_CTRL_CIRCLE) {
    controls_save();
    menu_open = 0;
    return 1;
  }
  if (cursor < ACT_COUNT) {
    if (pressed & SCE_CTRL_CROSS) capture = CAP_REPLACE;
    if (pressed & SCE_CTRL_SQUARE) capture = CAP_ADD;
    if (pressed & SCE_CTRL_TRIANGLE) binding[cursor] = 0;
  } else if (pressed & SCE_CTRL_CROSS) {
    if (cursor == ROW_VBUTTONS)
      set_virtual_buttons_visible(!g_vbuttons_visible);
    else if (cursor == ROW_FPS)
      set_fps_mode(!g_fps_original);
    else if (cursor == ROW_LANG) {
      int old = lang;
      lang = (lang + 1) % LANG_COUNT;
      set_text_language(langs[old].dir, langs[lang].dir);
    } else
      set_defaults();
  }
  return 0;
}

#define MENU_W 480
#define MENU_H 272
static uint32_t menu_px[MENU_W * MENU_H];
static GLuint menu_tex = 0;

// Colors are ABGR in memory (GL_RGBA/GL_UNSIGNED_BYTE, little endian).
#define C_BG 0xFF201008
#define C_SEL 0xFF603818
#define C_TEXT 0xFFE0E0E0
#define C_DIM 0xFF909090
#define C_HEAD 0xFF40C0FF

static void fill_row(int y, int h, uint32_t c) {
  for (int i = y * MENU_W; i < (y + h) * MENU_W; i++) menu_px[i] = c;
}

static void text(int x, int y, uint32_t c, const char *s) {
  const unsigned char *g = psvDebugScreenFont.glyphs;
  for (; *s && x + 8 <= MENU_W; s++, x += 8)
    for (int r = 0; r < 8; r++) {
      unsigned char bits = g[(unsigned char)*s * 8 + r];
      for (int col = 0; col < 8; col++)
        if (bits & (0x80 >> col)) menu_px[(y + r) * MENU_W + x + col] = c;
    }
}

static void binding_str(uint32_t m, char *out, size_t n) {
  out[0] = 0;
  for (int b = 0; b < BTN_NAME_COUNT; b++)
    if (m & btn_names[b].bit) {
      if (out[0]) strncat(out, ", ", n - strlen(out) - 1);
      strncat(out, btn_names[b].name, n - strlen(out) - 1);
    }
  if (!out[0]) strncpy(out, "NONE", n);
}

static void render(void) {
  fill_row(0, MENU_H, C_BG);
  text(16, 12, C_HEAD, "PS VITA CONTROLS");
  for (int r = 0; r < ROW_COUNT; r++) {
    int y = 36 + r * 16;
    if (r == cursor) fill_row(y - 4, 16, C_SEL);
    char val[96];
    if (r < ACT_COUNT) {
      text(16, y, C_TEXT, actions[r].label);
      if (r == cursor && capture != CAP_NONE)
        strcpy(val, capture == CAP_ADD ? "press a button to add..."
                                       : "press a button...");
      else
        binding_str(binding[r], val, sizeof(val));
      text(184, y, C_TEXT, val);
    } else if (r == ROW_VBUTTONS) {
      text(16, y, C_TEXT, "Touch D-pad/buttons (L+R)");
      text(232, y, C_TEXT, g_vbuttons_visible ? "SHOWN" : "HIDDEN");
    } else if (r == ROW_FPS) {
      text(16, y, C_TEXT, "Frame rate (Select+R)");
      if (g_fps_original)
        snprintf(val, sizeof(val), "ORIGINAL (%d)", g_engine_fps);
      else
        strcpy(val, "30 FPS");
      text(232, y, C_TEXT, val);
    } else if (r == ROW_LANG) {
      text(16, y, C_TEXT, "Text language");
      text(232, y, C_TEXT, langs[lang].label);
    } else {
      text(16, y, C_TEXT, "Restore defaults");
    }
  }
  text(16, 232, C_DIM, "X replace  [] add  /\\ unbind  O save & close");
  text(16, 248, C_DIM, "Rear touch: L2/R2 top, L3/R3 bottom quadrants");
}

GLuint controls_menu_texture(void) {
  if (!menu_tex) {
    glGenTextures(1, &menu_tex);
    glBindTexture(GL_TEXTURE_2D, menu_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    dirty = 1;
  }
  if (dirty) {
    render();
    glBindTexture(GL_TEXTURE_2D, menu_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, MENU_W, MENU_H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, menu_px);
    dirty = 0;
  }
  return menu_tex;
}
