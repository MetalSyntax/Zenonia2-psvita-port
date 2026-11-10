/*
 * main.c
 *
 * ARMv7 Shared Libraries loader. Zenonia 2.
 */

#include <math.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "controls.h"
#include "debugScreen.h"
#include "image_load.h"
#include "postprocess.h"
#include "so_util.h"
#include <falso_jni/FalsoJNI.h>
#include <taihen.h>
#include <vitaGL.h>

/**
 * @brief kubridge's unrestricted memcpy, declared manually instead of
 *        including kubridge.h in full.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
extern int kuKernelCpuUnrestrictedMemcpy(void *dst, const void *src,
                                         SceSize len);
extern void kuKernelFlushCaches(const void *ptr, SceSize len);

#define printf psvDebugScreenPrintf
#define LOG_DIR "ux0:data/zenonia-2/logs"

FILE *log_file = NULL;

/**
 * @brief Set once vitaGL takes ownership of the display.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
int gl_active = 0;

int _newlib_heap_size_user = 128 * 1024 * 1024; // 128 MB for newlib (malloc)
unsigned int sceLibcHeapSize =
    4 * 1024 * 1024; // 4 MB for SCE Libc (system libs)

/**
 * @brief Opens LOG_DIR/NNN.log for this run: highest existing number + 1
 *        (001-999; after 999 it wraps to 001 and overwrites).
 */
void init_log() {
  sceIoMkdir(LOG_DIR, 0777); // fails silently if it already exists

  int last = 0;
  SceUID dir = sceIoDopen(LOG_DIR);
  if (dir >= 0) {
    SceIoDirent ent;
    while (sceIoDread(dir, &ent) > 0) {
      int n;
      char ext[8];
      if (sscanf(ent.d_name, "%3d.%7s", &n, ext) == 2 &&
          strcmp(ext, "log") == 0 && n > last && n <= 999)
        last = n;
    }
    sceIoDclose(dir);
  }

  char log_path[256];
  snprintf(log_path, sizeof(log_path), LOG_DIR "/%03d.log", last % 999 + 1);

  log_file = fopen(log_path, "w");
  if (log_file) {
    fprintf(log_file, "--- ZENONIA 2 PORT LOG START (%s) ---\n", log_path);
    fflush(log_file);
  }
}

/**
 * @brief Formats and appends a line to log_file (file-only, no screen output).
 * @param fmt printf-style format string.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
void game_log(const char *fmt, ...) {
  va_list list;
  char string[512];

  va_start(list, fmt);
  vsnprintf(string, sizeof(string), fmt, list);
  va_end(list);

  if (log_file) {
    fprintf(log_file, "%s", string);
    fflush(log_file);
  }
}

void fatal_error(const char *fmt, ...) {
  va_list list;
  char string[512];

  va_start(list, fmt);
  vsnprintf(string, sizeof(string), fmt, list);
  va_end(list);

  game_log("[FATAL] %s\n", string);
  /**
   * @note debugScreen is lazily initialized here only. Ver docs/loader/main.md
   *       para el razonamiento de diseño.
   */
  psvDebugScreenInit();
  printf("[FATAL] %s\n", string);
  sceKernelDelayThread(10 * 1000 * 1000); // 10s so it's readable before dying
  sceKernelExitProcess(0);
}

so_module zenonia2_mod;

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  va_list list;
  char string[512];

  va_start(list, fmt);
  vsnprintf(string, sizeof(string), fmt, list);
  va_end(list);

  game_log("[ANDROID] %s: %s\n", tag, string);
  return 0;
}

extern so_default_dynlib default_dynlib[];
extern int default_dynlib_size;

// Game JNI Pointers
int (*Game_JNI_OnLoad)(void *vm, void *reserved);
void (*NativeInit)(void *env, void *obj);
void (*NativeRender)(void *env, void *obj);
void (*NativeResize)(void *env, void *obj, int w, int h);
void (*setInputEvent)(void *env, void *obj, int type, int p1, int p2);
void (*NativeResumeClet)(void *env, void *obj);
void (*handleCletEvent)(void *env, void *obj, int type, int p1, int p2);

// Virtual buttons JNI Pointers
void (*SetDpadDrawingBlock)(void *env, void *obj, int block) = NULL;
void (*SetShowDirectionButton)(void *env, void *obj, int show) = NULL;
void (*SetShowSelectButton)(void *env, void *obj, int show) = NULL;
void (*SetShowBackButton)(void *env, void *obj, int show) = NULL;
void (*SetShowGameMenuButton)(void *env, void *obj, int show) = NULL;
void (*SetShowMapButton)(void *env, void *obj, int show) = NULL;
void (*SetShowSkipButton)(void *env, void *obj, int show) = NULL;
void (*SetShowSaveButton)(void *env, void *obj, int show) = NULL;
void (*SetShowResetButton)(void *env, void *obj, int show) = NULL;
void (*SetDpadPosition)(void *env, void *obj, int x, int y, int size) = NULL;
void (*SetButtonPosition)(void *env, void *obj, int x, int y, int size) = NULL;

void update_virtual_buttons(int status) {
  if (!SetShowDirectionButton) return;
  void *env = &jni;

  if (status >= 3 && g_vbuttons_visible) {
    // In-game: gameplay (3) y subpantallas de juego (6, 8, etc.)
    if (SetDpadDrawingBlock) SetDpadDrawingBlock(env, NULL, 0);
    if (SetDpadPosition) SetDpadPosition(env, NULL, 4, 123, 1);
    if (SetButtonPosition) SetButtonPosition(env, NULL, 325, 163, 1);
    if (SetShowDirectionButton) SetShowDirectionButton(env, NULL, 1);
    if (SetShowSelectButton) SetShowSelectButton(env, NULL, 1);
    if (SetShowGameMenuButton) SetShowGameMenuButton(env, NULL, (status == 3) ? 1 : 0);
    if (SetShowMapButton) SetShowMapButton(env, NULL, (status == 3) ? 1 : 0);
    if (SetShowSaveButton) SetShowSaveButton(env, NULL, (status == 3) ? 1 : 0);
    if (SetShowBackButton) SetShowBackButton(env, NULL, (status == 6 || status == 8) ? 1 : 0);
    if (SetShowSkipButton) SetShowSkipButton(env, NULL, (status == 7) ? 1 : 0);
  } else {
    // Menú principal (2), Título (1), Logo (0): completamente apagados
    if (SetShowDirectionButton) SetShowDirectionButton(env, NULL, 0);
    if (SetShowSelectButton) SetShowSelectButton(env, NULL, 0);
    if (SetShowGameMenuButton) SetShowGameMenuButton(env, NULL, 0);
    if (SetShowMapButton) SetShowMapButton(env, NULL, 0);
    if (SetShowSaveButton) SetShowSaveButton(env, NULL, 0);
    if (SetShowBackButton) SetShowBackButton(env, NULL, 0);
    if (SetShowSkipButton) SetShowSkipButton(env, NULL, 0);
    if (SetShowResetButton) SetShowResetButton(env, NULL, 0);
  }
}

/**
 * @defgroup input Input handling
 * @brief APK input protocol replica: each event is queued via
 *        queue_input_event() (setInputEvent) and later drained once per
 *        frame via handleCletEvent() in the main loop. Touch coordinates are
 *        in the game's internal 400x240 space, not screen pixels.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */

// Eventos MH_* (NexusHal.java)
#define MH_KEY_PRESSEVENT 2
#define MH_KEY_RELEASEEVENT 3
#define MH_POINTER_PRESSEVENT 23
#define MH_POINTER_RELEASEEVENT 24

// Keycodes HAL (Zenonia2UIControllerView.getHalKeyCode + UI*Button.java)
#define HAL_KEY_UP (-1)
#define HAL_KEY_DOWN (-2)
#define HAL_KEY_LEFT (-3)
#define HAL_KEY_RIGHT (-4)
#define HAL_KEY_OK (-5)    // UISelectButton
#define HAL_KEY_MAP (-6)   // UIMapButton
#define HAL_KEY_SAVE (-10) // UISaveButton
#define HAL_KEY_BACK (-16) // UIBackButton / UIMenuButton
#define HAL_KEY_SKIP (35)  // UISkipButton / UIQuickSlotButton

typedef struct {
  int type, p1, p2;
} input_event;
static input_event event_queue[16];
static int eq_head = 0, eq_tail = 0;

static void queue_input_event(void *env, int type, int p1, int p2) {
  static int in_log = 0;
  if (in_log < 40) {
    game_log("[INPUT] event type=%d p1=%d p2=%d\n", type, p1, p2);
    in_log++;
  }
  if (setInputEvent)
    setInputEvent(env, NULL, type, p1, p2);
  int next = (eq_tail + 1) % 16;
  if (next != eq_head) {
    event_queue[eq_tail].type = type;
    event_queue[eq_tail].p1 = p1;
    event_queue[eq_tail].p2 = p2;
    eq_tail = next;
  }
}


extern volatile int g_ui_status;
static int vbtn_patch_ok = 0;
static uintptr_t vbtn_text_base = 0;

/**
 * @brief Shows/hides the engine's touch D-pad and buttons (L+R or the
 *        controls menu): patches their draw functions and the engine's
 *        show flags, and enables/disables the loader's touch hit zones.
 */
void set_virtual_buttons_visible(int visible) {
  if (!vbtn_patch_ok) visible = 1; // unknown .so: never patched, always drawn
  const uint16_t insn = visible ? 0xb5f0 : 0x4770; // push {r4-r7,lr} : bx lr
  if (vbtn_patch_ok) {
    void *dpad = (void *)(vbtn_text_base + 0x52980);
    void *btn = (void *)(vbtn_text_base + 0x52a50);
    kuKernelCpuUnrestrictedMemcpy(dpad, &insn, sizeof(insn));
    kuKernelCpuUnrestrictedMemcpy(btn, &insn, sizeof(insn));
    kuKernelFlushCaches(dpad, sizeof(insn));
    kuKernelFlushCaches(btn, sizeof(insn));
  }
  g_vbuttons_visible = visible;
  update_virtual_buttons(g_ui_status);
  game_log("[VBUTTONS] %s\n", visible ? "visibles" : "ocultos");
}

/**
 * @brief Applies confirmed binary patches to the loaded .so in place.
 * @param mod The loaded Zenonia 2 module (already relocated/resolved).
 * @pre Must run after so_relocate()/so_resolve() and before so_flush_caches().
 * @note Patches CMvLayerData::PreLoad (VA 0xaec38): `ble` -> `beq`, so the
 *       map-layer NULL check is unsigned-safe on Vita's heap addresses.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
static void apply_so_patches(so_module *mod) {
  const uint16_t ble = 0xdd27, beq = 0xd027;
  uint16_t cur = *(uint16_t *)(mod->text_base + 0xaec38);
  if (cur != ble) {
    // .so distinto del analizado (md5 cae9d5fe...) -- no tocar a ciegas
    game_log("[PATCH] AVISO: bytes inesperados en 0xaec38 (0x%04x, se esperaba "
             "0x%04x) -- parche omitido\n",
             cur, ble);
    return;
  }
  kuKernelCpuUnrestrictedMemcpy((void *)(mod->text_base + 0xaec38), &beq,
                                sizeof(beq));
  game_log("Parche aplicado: CMvLayerData::PreLoad ble->beq @ 0x%08x\n",
           (unsigned int)(mod->text_base + 0xaec38));

  /**
   * @brief drawDpad (0x52980) / drawButton (0x52a50) are toggled between their
   *        original first instruction and `bx lr` at runtime (L+R, see
   *        set_virtual_buttons_visible), so the touch overlay can be hidden.
   */
  const uint16_t push_r4_r7_lr = 0xb5f0;
  vbtn_patch_ok = *(uint16_t *)(mod->text_base + 0x52980) == push_r4_r7_lr &&
                  *(uint16_t *)(mod->text_base + 0x52a50) == push_r4_r7_lr;
  vbtn_text_base = mod->text_base;
  if (!vbtn_patch_ok)
    game_log("[PATCH] AVISO: bytes inesperados en drawDpad/drawButton -- "
             "botones virtuales siempre visibles\n");
#ifdef HIDE_VIRTUAL_BUTTONS
  set_virtual_buttons_visible(0);
#else
  set_virtual_buttons_visible(1);
#endif
}

/**
 * @brief Swaps the UI strings hardcoded in the .so (menus, inventory, stats)
 *        using data/<lang>/libstrings.txt ("vaddr\tslot\ten\ttr" per line,
 *        written by scripts/zt1_lang.py): en->tr, or tr->en when @p revert.
 *        A line is applied only if the expected text and its NUL padding up
 *        to the slot are found intact, so a different .so build or a stale
 *        file can't corrupt memory.
 */
static void lang_so_strings(const char *dir, int revert) {
  uintptr_t base = zenonia2_mod.text_base;
  char path[128], line[512], buf[256];
  snprintf(path, sizeof(path), "ux0:data/zenonia-2/assets/data/%s/libstrings.txt", dir);
  FILE *f = fopen(path, "r");
  if (!f) {
    game_log("[LANG] %s no encontrado, menus en ingles\n", path);
    return;
  }
  int ok = 0, bad = 0;
  while (fgets(line, sizeof(line), f)) {
    char *a = strtok(line, "\t"), *c = strtok(NULL, "\t");
    char *en = strtok(NULL, "\t"), *tr = strtok(NULL, "\r\n");
    if (!tr)
      continue;
    if (revert) {
      char *t = en;
      en = tr;
      tr = t;
    }
    uintptr_t off = strtoul(a, NULL, 16);
    size_t cap = strtoul(c, NULL, 10), le = strlen(en), lt = strlen(tr);
    char *p = (char *)(base + off);
    // .rodata starts at 0xe24d8, .data ends at 0x101b2c (md5 cae9d5fe...)
    int match = off >= 0xe24d8 && off + cap < 0x101b2c && cap < sizeof(buf) &&
                lt <= cap && le <= cap && memcmp(p, en, le + 1) == 0;
    for (size_t i = le + 1; match && i <= cap; i++)
      match = p[i] == 0;
    if (!match) {
      bad++;
      continue;
    }
    memset(buf, 0, cap + 1);
    memcpy(buf, tr, lt);
    kuKernelCpuUnrestrictedMemcpy(p, buf, cap + 1);
    ok++;
  }
  fclose(f);
  game_log("[LANG] %s%s: %d textos del .so, %d omitidos\n", revert ? "-" : "+",
           dir, ok, bad);
}

/**
 * @brief Overwrites the loaded Xls* tables in place with the @p dir version
 *        (NULL = English). GXL tables are fixed-width rows and the translated
 *        files only differ in text bytes, so the size and header match and
 *        pointers into the old buffer stay valid. The engine never writes them
 *        (GVXLLoader::SetVal has no callers).
 */
static void lang_reload_xls(const char *dir) {
  static const char *names[] = {"XlsChar", "XlsChargeItem", "XlsItem",
                                "XlsMap",  "XlsNPC",        "XlsPCNew",
                                "XlsQUEST", "XlsSetItem",   "XlsSkill"};
  uintptr_t single = so_symbol(&zenonia2_mod, "_ZN12CGsSingletonI9CMvXlsMgrE13ms_pSingletonE");
  int (*unz)(void *, unsigned long *, const void *, unsigned long) =
      (void *)so_symbol(&zenonia2_mod, "uncompress");
  uint8_t *mgr = single ? *(uint8_t **)single : NULL;
  if (!mgr || !unz)
    return; // before NativeInit: tables load from the right dir anyway
  int count = *(int *)(mgr + 4), ok = 0;
  uint8_t **tbls = *(uint8_t ***)(mgr + 8);
  for (int n = 0; n < (int)(sizeof(names) / sizeof(names[0])); n++) {
    char path[128];
    snprintf(path, sizeof(path), "ux0:data/zenonia-2/assets/data/%s/%s.zt1",
             dir ? dir : "eng", names[n]);
    FILE *f = fopen(path, "rb");
    uint32_t hdr[2]; // zt1: file size, decompressed size, zlib stream
    if (!f || fread(hdr, 4, 2, f) != 2 || hdr[0] <= 8 || hdr[1] > (1 << 22)) {
      if (f)
        fclose(f);
      continue;
    }
    unsigned long zlen = hdr[0] - 8, ulen = hdr[1];
    uint8_t *z = malloc(zlen), *u = malloc(ulen);
    if (z && u && fread(z, 1, zlen, f) == zlen && unz(u, &ulen, z, zlen) == 0) {
      uint16_t cols = *(uint16_t *)(u + 6);
      // Unique loaded table with the same GXL header + column types and size
      uint8_t *dst = NULL;
      int hits = 0;
      for (int i = 0; i < count; i++) {
        uint8_t *t = tbls[i], *buf = t ? *(uint8_t **)t : NULL;
        if (!buf)
          continue;
        size_t sz = *(uint16_t *)(t + 0xc) + 10 +
                    (size_t)*(uint16_t *)(t + 8) * *(uint16_t *)(t + 0xa);
        if (sz == ulen && memcmp(buf, u, 10 + cols) == 0) {
          dst = buf;
          hits++;
        }
      }
      if (hits == 1) {
        memcpy(dst, u, ulen);
        ok++;
      } else
        game_log("[LANG] %s: %d tablas coinciden, no se recarga\n", names[n], hits);
    }
    free(z);
    free(u);
    fclose(f);
  }
  game_log("[LANG] %d tablas Xls recargadas\n", ok);
}

/**
 * @brief Switches the text language (data/ subdir, NULL = English) at boot or
 *        live from the Start+Select menu: .so strings, Xls tables in place,
 *        and Str tables closed so CMvStrMgr::GetTbl lazily reopens them from
 *        the new dir (java.c reads controls_lang_dir()) — the engine itself
 *        calls CloseTblAll on every script load, so callers already cope.
 * @note ponytail: strings the engine copied into its own buffers (e.g. a
 *       window already open) keep the old language until rebuilt.
 */
void set_text_language(const char *old, const char *dir) {
  if (old)
    lang_so_strings(old, 1);
  if (dir)
    lang_so_strings(dir, 0);
  if (old == dir)
    return; // same language (or English boot): nothing to reload
  lang_reload_xls(dir);
  uintptr_t single = so_symbol(&zenonia2_mod, "_ZN12CGsSingletonI9CMvStrMgrE13ms_pSingletonE");
  void (*close_all)(void *) =
      (void *)so_symbol(&zenonia2_mod, "_ZN9CMvStrMgr11CloseTblAllEv");
  if (single && *(void **)single && close_all)
    close_all(*(void **)single);
  game_log("[LANG] idioma: %s\n", dir ? dir : "eng");
}

/**
 * @brief UI status published by java.c: 0 = logo, 1 = title, 2 = menu,
 *        >=3 = in-game. Drives which splash overlay (if any) main() draws
 *        on top of the engine's own output each frame.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
extern volatile int g_ui_status;

/**
 * @brief Directory holding the APK's original splash drawables, deployed
 *        separately from the VPK and decoded at runtime (see image_load.c).
 */
#define DRAWABLE_DIR "ux0:data/zenonia-2/drawable"

static GLuint logo_tex = 0;
static GLuint title_tex = 0;
static GLuint touch_tex = 0;
/**
 * @brief Cover-fit scale factor shared by title.png and touch.png (800x480
 *        source -> 960x544 target).
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
#define TITLE_COVER_SCALE 1.2f
#define TOUCH_TEX_W 310
#define TOUCH_TEX_H 30
#define TOUCH_TEX_X ((960 - TOUCH_TEX_W) / 2)
#define TOUCH_TEX_Y ((544 * 3) / 4)

static void splash_load(void) {
  logo_tex = image_load_png_tex(DRAWABLE_DIR "/logo.png", 960, 544, 0.0f);
  title_tex = image_load_png_tex(DRAWABLE_DIR "/title.png", 960, 544, 0.0f);
  touch_tex = image_load_png_tex(DRAWABLE_DIR "/touch.png", TOUCH_TEX_W,
                                 TOUCH_TEX_H, TITLE_COVER_SCALE);
}

// Se dibuja DESPUES de NativeRender (tapa el blanco del motor) preservando las
// matrices con push/pop; el resto del estado GL el motor lo re-setea por frame.
static void splash_draw(GLuint tex) {
  if (!tex)
    return;

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrthof(0, 960, 544, 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  static const float verts[] = {0, 0, 960, 0, 0, 544, 960, 544};
  static const float uvs[] = {0, 0, 1, 0, 0, 1, 1, 1};
  glVertexPointer(2, GL_FLOAT, 0, verts);
  glTexCoordPointer(2, GL_FLOAT, 0, uvs);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
}

/**
 * @brief Draws the pulsing "touch to continue" prompt over the title screen.
 * @param frame Monotonic frame counter, used to phase the alpha pulse.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
static void touch_draw(int frame) {
  if (!touch_tex)
    return;

  const float alpha = 0.35f + 0.65f * (0.5f + 0.5f * sinf(frame * 0.05f));

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrthof(0, 960, 544, 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, touch_tex);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glColor4f(1.0f, 1.0f, 1.0f, alpha);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  /**
   * @note `static`: vitaGL references vertex arrays lazily (GXM command
   *       buffer), so a stack-local array here would be stale by draw time.
   *       Ver docs/loader/main.md para el razonamiento de diseño.
   */
  static const float verts[] = {TOUCH_TEX_X,
                                TOUCH_TEX_Y,
                                TOUCH_TEX_X + TOUCH_TEX_W,
                                TOUCH_TEX_Y,
                                TOUCH_TEX_X,
                                TOUCH_TEX_Y + TOUCH_TEX_H,
                                TOUCH_TEX_X + TOUCH_TEX_W,
                                TOUCH_TEX_Y + TOUCH_TEX_H};
  static const float uvs[] = {0, 0, 1, 0, 0, 1, 1, 1};
  glVertexPointer(2, GL_FLOAT, 0, verts);
  glTexCoordPointer(2, GL_FLOAT, 0, uvs);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  glDisable(GL_BLEND);
  /**
   * @note Restores GL_TEXTURE_ENV_MODE to GL_REPLACE, the mode the engine
   *       itself expects for every subsequent frame's compositor quad.
   * @note Ver docs/loader/main.md para el razonamiento de diseño.
   */
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
}

/**
 * @brief Logs the framebuffer currently reported by sceDisplayGetFrameBuf.
 * @param label Prefix identifying the call site in the log.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
void log_active_frame_buf(const char *label) {
  SceDisplayFrameBuf fb;
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  int ret = sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
  game_log("[DISPLAY] %s: sceDisplayGetFrameBuf ret=0x%08x base=%p w=%d h=%d "
           "pitch=%d\n",
           label, ret, fb.base, fb.width, fb.height, fb.pitch);
}

/**
 * @brief Frame-rate mode: 30 FPS = eglSwapInterval(2) (confirmed stable,
 *        Fase 20.2); ORIGINAL = the engine's SetSpeed value, paced like the
 *        Android NexusGLThread (period 1000/fps ms) on top of interval 1.
 * @note The deadline pacer is only used here: at <=22 FPS the period has
 *       >=10 ms slack for the vblank wait, unlike at 30 (see Fase 20.2).
 */
extern volatile int g_engine_fps;
void set_fps_mode(int original) {
  g_fps_original = original;
  eglSwapInterval(eglGetDisplay(EGL_DEFAULT_DISPLAY), original ? 1 : 2);
  game_log("[FPS] modo %s (motor pide %d)\n", original ? "ORIGINAL" : "30",
           g_engine_fps);
}

static void original_pace(void) {
  static SceUInt64 deadline = 0;
  int fps = g_engine_fps;
  if (!g_fps_original || fps <= 0) {
    deadline = 0;
    return;
  }
  SceUInt64 period = (SceUInt64)(1000 / fps) * 1000; // ms granularity, as Java
  SceUInt64 now = sceKernelGetProcessTimeWide();
  if (deadline == 0 || now > deadline + period)
    deadline = now; // first frame / long stall: resync, don't burst
  else if (deadline > now)
    sceKernelDelayThread(deadline - now);
  deadline += period;
}

/**
 * @brief Initializes vitaGL (no MSAA, no triple buffering) and caps the
 *        swap rate to ~30 FPS via real VSync.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
void gl_init() {
  vglUseTripleBuffering(GL_FALSE);
  /** @note GL_FALSE here is the expected result at native 960x544, not a
   *        failure code — see docs/loader/main.md. */
  vglInitExtended(0, 960, 544, 6 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);

  /** @note interval=2 caps to ~30 FPS (2 vblanks/swap). A software deadline
   *        pacer on top of interval=1 was tried (Fase 20) and dropped to 20 FPS:
   *        sleeping to its deadline then blocking on the next vblank made most
   *        frames 3 vblanks long. */
  eglSwapInterval(eglGetDisplay(EGL_DEFAULT_DISPLAY), 2);

  gl_active = 1;
}

/**
 * @brief Entry point: boosts clocks, loads/relocates/patches the .so,
 *        initializes vitaGL/audio/JNI, and runs the main render/input loop.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
int main() {
  /** @note Standard homebrew clock boost (default 333/111/166MHz). */
  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(166);
  scePowerSetGpuXbarClockFrequency(166);
  // Render/engine on core 0, audio mixer on core 1 (see audio_init)
  sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(),
                                       SCE_KERNEL_CPU_MASK_USER_0);
  init_log();
  game_log("Iniciando Zenonia 2 port (SoLoader)\n");
  // Cargar la libreria SO en memoria
  int res = so_file_load(&zenonia2_mod, "ux0:data/zenonia-2/libzenonia2.so",
                         0x98000000);
  if (res < 0) {
    game_log("Error critico cargando libzenonia2.so: 0x%08X\n", res);
    sceKernelDelayThread(5000000); // 5 segundos para que se pueda leer el error
  } else {
    game_log("Libreria cargada con exito.\n");
    game_log("mod: text_base=0x%08x num_dynsym=%d dynsym=%p dynstr=%p hash=%p "
             "soname=%s\n",
             (unsigned int)zenonia2_mod.text_base, zenonia2_mod.num_dynsym,
             (void *)zenonia2_mod.dynsym, (void *)zenonia2_mod.dynstr,
             (void *)zenonia2_mod.hash,
             zenonia2_mod.soname ? zenonia2_mod.soname : "(null)");

    // Relocalizacion y Resolucion de dependencias
    so_relocate(&zenonia2_mod);
    so_resolve(&zenonia2_mod, default_dynlib, default_dynlib_size, 0);

    // Parches al codigo del juego (antes de flushear caches)
    apply_so_patches(&zenonia2_mod);

    // Inicializar
    so_flush_caches(&zenonia2_mod);
    so_initialize(&zenonia2_mod);

    game_log("SoLoader inicializado. Iniciando vitaGL...\n");
    gl_init();
    game_log("vitaGL inicializado.\n");
    splash_load();
    postprocess_init(); // no-op salvo build con ENABLE_POSTPROCESS_SHADER
    audio_init();

    jni_init();
    JNIEnv *jniEnv = &jni;

    // Obtener punteros de funciones JNI
    Game_JNI_OnLoad = (void *)so_symbol(&zenonia2_mod, "JNI_OnLoad");
    NativeInit = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_NativeInit");
    NativeRender = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_NativeRender");
    NativeResize = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_NativeResize");
    setInputEvent = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_setInputEvent");
    NativeResumeClet = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_NativeResumeClet");
    handleCletEvent = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_handleCletEvent");

    SetDpadDrawingBlock = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_SetDpadDrawingBlock");
    SetShowDirectionButton = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_SetShowDirectionButton");
    SetShowSelectButton = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_SetShowSelectButton");
    SetShowBackButton = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_SetShowBackButton");
    SetShowGameMenuButton = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_SetShowGameMenuButton");
    SetShowMapButton = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_SetShowMapButton");
    SetShowSkipButton = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_SetShowSkipButton");
    SetShowSaveButton = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_SetShowSaveButton");
    SetShowResetButton = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_SetShowResetButton");
    SetDpadPosition = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_SetDpadPosition");
    SetButtonPosition = (void *)so_symbol(
        &zenonia2_mod, "Java_com_gamevil_nexus2_Natives_SetButtonPosition");

    game_log(
        "Symbols: JNI_OnLoad=%p NativeInit=%p NativeRender=%p NativeResize=%p "
        "setInputEvent=%p NativeResumeClet=%p handleCletEvent=%p ShowDpad=%p ShowBtn=%p\n",
        (void *)Game_JNI_OnLoad, (void *)NativeInit, (void *)NativeRender,
        (void *)NativeResize, (void *)setInputEvent, (void *)NativeResumeClet,
        (void *)handleCletEvent, (void *)SetShowDirectionButton,
        (void *)SetShowSelectButton);

    controls_init(); // before NativeInit: LANG must be known before any data/eng read
    set_text_language(NULL, controls_lang_dir());
    // Ejecutar la secuencia de inicio de Android
    game_log("Llamando JNI_OnLoad...\n");
    if (Game_JNI_OnLoad)
      Game_JNI_OnLoad(&jvm, NULL);
    game_log("Llamando NativeInit...\n");
    if (NativeInit)
      NativeInit(jniEnv, NULL);
    if (SetDpadDrawingBlock)
      SetDpadDrawingBlock(jniEnv, NULL, 0);
    if (SetDpadPosition)
      SetDpadPosition(jniEnv, NULL, 4, 123, 1);
    if (SetButtonPosition)
      SetButtonPosition(jniEnv, NULL, 325, 163, 1);
    game_log("Botones virtuales inicializados: DpadPos=%p BtnPos=%p ShowDpad=%p\n",
             (void *)SetDpadPosition, (void *)SetButtonPosition,
             (void *)SetShowDirectionButton);
    game_log("Llamando NativeResize...\n");
    if (NativeResize)
      NativeResize(jniEnv, NULL, 960, 544);
    game_log("Llamando NativeResumeClet...\n");
    if (NativeResumeClet)
      NativeResumeClet(jniEnv, NULL);

    game_log("Iniciando Bucle Principal...\n");

    // Habilitar muestreo táctil
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
                             SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

    SceCtrlData pad;
    SceTouchData touch;
    int last_touch = 0;
    int last_tx = 0, last_ty = 0;
    int active_touch_key = 0;
    unsigned int old_buttons = 0, old_game_buttons = 0, suppress = 0;
    int frame = 0;
    int last_ui_status = -1;
    SceUInt64 stat_t0 = 0, render_us = 0, render_max_us = 0; // FPS telemetry

    while (1) {
      if (g_ui_status != last_ui_status) {
        last_ui_status = g_ui_status;
        update_virtual_buttons(g_ui_status);
      }
      sceCtrlPeekBufferPositive(0, &pad, 1);
      sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

      if ((frame++ % 120) == 0) {
        SceUInt64 now = sceKernelGetProcessTimeWide();
        float fps = stat_t0 ? 120.0f * 1000000.0f / (float)(now - stat_t0) : 0;
        game_log("frame %d alive, fps=%.1f render_avg=%.1fms render_max=%.1fms "
                 "ui_status=%d\n",
                 frame, fps, render_us / 120 / 1000.0f, render_max_us / 1000.0f,
                 g_ui_status);
        stat_t0 = now;
        render_us = render_max_us = 0;
      }

      // --- Botones fisicos -> acciones remapeables (controls.c) ---
      unsigned int buttons = controls_read(&pad);
      unsigned int pressed = buttons & ~old_buttons;
#define COMBO_HELD(m) ((buttons & (m)) == (m) && (old_buttons & (m)) != (m))
      // Buttons held through a combo or the menu closing stay out of the game
      // until released (else closing with O would also press BACK, etc.).
      // An L tap just before R may still fire its action, as in Zenonia 3.
      if (COMBO_HELD(SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) {
        set_virtual_buttons_visible(!g_vbuttons_visible);
        suppress |= SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
      }
      if (COMBO_HELD(SCE_CTRL_SELECT | SCE_CTRL_RTRIGGER)) {
        set_fps_mode(!g_fps_original);
        controls_save();
        suppress |= SCE_CTRL_SELECT | SCE_CTRL_RTRIGGER;
      }
      if (COMBO_HELD(SCE_CTRL_START | SCE_CTRL_SELECT) && !controls_menu_active())
        controls_menu_open();
      if (controls_menu_active() && controls_menu_update(buttons, pressed))
        suppress |= buttons; // menu just closed
      suppress &= buttons;
      unsigned int game_buttons =
          controls_menu_active() ? 0 : (buttons & ~suppress);
      for (int a = 0; a < ACT_COUNT; a++) {
        int was = (old_game_buttons & controls_binding(a)) != 0;
        int is = (game_buttons & controls_binding(a)) != 0;
        if (is != was)
          queue_input_event(jniEnv, is ? MH_KEY_PRESSEVENT : MH_KEY_RELEASEEVENT,
                            controls_action_hal(a), 0);
      }
      old_buttons = buttons;
      old_game_buttons = game_buttons;

      // --- Touch: panel 1920x1088 -> espacio del juego 400x240 ---
      if (touch.reportNum > 0) {
        int x = touch.report[0].x * 400 / 1920;
        int y = touch.report[0].y * 240 / 1088;
        last_tx = x;
        last_ty = y;

        if (controls_menu_active()) {
          // menu open: front touch ignored
        } else if (!last_touch) {
          if (g_ui_status >= 3 && g_vbuttons_visible) {
            // Mapeo táctil a botones virtuales durante in-game
            if (x <= 55 && y <= 50) {
              // Pergamino / Mapa (esquina superior izquierda)
              active_touch_key = (g_ui_status == 3) ? HAL_KEY_MAP : 0;
            } else if (x >= 345 && y <= 50) {
              // Bolso / Menú / Back (esquina superior derecha)
              active_touch_key = HAL_KEY_BACK;
            } else if (x >= 170 && x <= 230 && y <= 50) {
              // Guardar / Save (arriba al centro)
              active_touch_key = (g_ui_status == 3) ? HAL_KEY_SAVE : 0;
            } else if (x >= 310 && y >= 140) {
              // Botón de ataque / acción (esquina inferior derecha)
              active_touch_key = HAL_KEY_OK;
            } else if (x <= 130 && y >= 110) {
              // D-Pad virtual (esquina inferior izquierda)
              int dx = x - 58;
              int dy = y - 178;
              if (abs(dx) > abs(dy)) {
                active_touch_key = (dx > 0) ? HAL_KEY_RIGHT : HAL_KEY_LEFT;
              } else {
                active_touch_key = (dy > 0) ? HAL_KEY_DOWN : HAL_KEY_UP;
              }
            } else {
              active_touch_key = 0;
            }

            if (active_touch_key != 0) {
              queue_input_event(jniEnv, MH_KEY_PRESSEVENT, active_touch_key, 0);
            } else {
              queue_input_event(jniEnv, MH_POINTER_PRESSEVENT, x, y);
            }
          } else {
            active_touch_key = 0;
            queue_input_event(jniEnv, MH_POINTER_PRESSEVENT, x, y);
          }
          last_touch = 1;
        }
      } else if (last_touch) {
        if (active_touch_key != 0) {
          queue_input_event(jniEnv, MH_KEY_RELEASEEVENT, active_touch_key, 0);
          active_touch_key = 0;
        } else {
          queue_input_event(jniEnv, MH_POINTER_RELEASEEVENT, last_tx, last_ty);
        }
        last_touch = 0;
      }

      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      // Como NexusGLRenderer.drawFrame, antes de NativeRender. Se entregan
      // TODOS los pendientes: de a uno por frame la cola se atrasaba (varios
      // botones a la vez en combate) y el input llegaba frames tarde.
      while (handleCletEvent && eq_head != eq_tail) {
        input_event *ev = &event_queue[eq_head];
        eq_head = (eq_head + 1) % 16;
        handleCletEvent(jniEnv, NULL, ev->type, ev->p1, ev->p2);
      }

      // Renderizar el frame
      SceUInt64 r0 = sceKernelGetProcessTimeWide();
      if (NativeRender && !controls_menu_active()) // menu pauses the game
        NativeRender(jniEnv, NULL);
      SceUInt64 rdt = sceKernelGetProcessTimeWide() - r0;
      render_us += rdt;
      if (rdt > render_max_us)
        render_max_us = rdt;

      /**
       * @note Overlays the real logo/title splash while g_ui_status is 0/1.
       *       Ver docs/loader/main.md para el razonamiento de diseño.
       */
      if (g_ui_status == 0) {
        splash_draw(logo_tex);
      } else if (g_ui_status == 1) {
        splash_draw(title_tex);
        touch_draw(frame);
      }
      if (controls_menu_active())
        splash_draw(controls_menu_texture());

      original_pace();
      // Intercambiar buffers en vitaGL
      vglSwapBuffers(GL_FALSE);
    }
  }

  if (log_file) {
    fclose(log_file);
  }
  sceKernelExitProcess(0);
  return 0;
}