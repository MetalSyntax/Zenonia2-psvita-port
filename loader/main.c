/*
 * main.c
 *
 * ARMv7 Shared Libraries loader. Zenonia 2.
 */

#include <math.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/dirent.h>
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
 * @brief Opens a new timestamped log file under LOG_DIR for this run.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
void init_log() {
  sceIoMkdir(LOG_DIR, 0777); // fails silently if it already exists

  char log_path[256];
  time_t t = time(NULL);
  snprintf(log_path, sizeof(log_path), LOG_DIR "/log_%u.txt", (unsigned int)t);

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
#ifndef HIDE_VIRTUAL_BUTTONS
  if (!SetShowDirectionButton) return;
  void *env = &jni;

  if (status >= 3) {
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
#endif
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

/**
 * @brief Maps physical Vita buttons to the HAL keycodes Android's on-screen
 *        touch UI would otherwise generate.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
static const struct {
  unsigned int btn;
  int hal;
} btn_map[] = {
    {SCE_CTRL_UP, HAL_KEY_UP},         {SCE_CTRL_DOWN, HAL_KEY_DOWN},
    {SCE_CTRL_LEFT, HAL_KEY_LEFT},     {SCE_CTRL_RIGHT, HAL_KEY_RIGHT},
    {SCE_CTRL_CROSS, HAL_KEY_OK},      {SCE_CTRL_CIRCLE, HAL_KEY_BACK},
    {SCE_CTRL_TRIANGLE, HAL_KEY_SKIP}, {SCE_CTRL_SQUARE, HAL_KEY_MAP},
    {SCE_CTRL_LTRIGGER, HAL_KEY_SAVE},
};
#define BTN_MAP_COUNT (sizeof(btn_map) / sizeof(btn_map[0]))

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

#ifdef HIDE_VIRTUAL_BUTTONS
  /**
   * @brief Patches drawDpad (0x52980) and drawButton (0x52a50) to `bx lr`
   *        so the mobile touch overlay is never drawn.
   * @note Ver docs/loader/main.md para el razonamiento de diseño.
   */
  const uint16_t bx_lr = 0x4770;
  const uint16_t dpad_expected = 0xb5f0; // push {r4, r5, r6, r7, lr}
  uint16_t dpad_cur = *(uint16_t *)(mod->text_base + 0x52980);
  if (dpad_cur == dpad_expected) {
    kuKernelCpuUnrestrictedMemcpy((void *)(mod->text_base + 0x52980), &bx_lr,
                                  sizeof(bx_lr));
    game_log("Parche aplicado: drawDpad -> bx lr @ 0x%08x (botones virtuales ocultos)\n",
             (unsigned int)(mod->text_base + 0x52980));
  } else {
    game_log("[PATCH] AVISO: bytes inesperados en drawDpad 0x52980 (0x%04x) -- omitido\n",
             dpad_cur);
  }

  uint16_t btn_cur = *(uint16_t *)(mod->text_base + 0x52a50);
  if (btn_cur == dpad_expected) {
    kuKernelCpuUnrestrictedMemcpy((void *)(mod->text_base + 0x52a50), &bx_lr,
                                  sizeof(bx_lr));
    game_log("Parche aplicado: drawButton -> bx lr @ 0x%08x (botones virtuales ocultos)\n",
             (unsigned int)(mod->text_base + 0x52a50));
  } else {
    game_log("[PATCH] AVISO: bytes inesperados en drawButton 0x52a50 (0x%04x) -- omitido\n",
             btn_cur);
  }
#else
  game_log("[PATCH] Botones virtuales habilitados (HUD táctil visible)\n");
#endif
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
 * @brief Initializes vitaGL (no MSAA, no triple buffering) and caps the
 *        swap rate to ~30 FPS via real VSync.
 * @note Ver docs/loader/main.md para el razonamiento de diseño.
 */
void gl_init() {
  vglUseTripleBuffering(GL_FALSE);
  /** @note GL_FALSE here is the expected result at native 960x544, not a
   *        failure code — see docs/loader/main.md. */
  vglInitExtended(0, 960, 544, 6 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);

  /** @note interval=2 caps to ~30 FPS (2 vblanks/swap @ ~59.94Hz) — ver
   *        docs/loader/main.md para el razonamiento de diseño. */
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

    // Ejecutar la secuencia de inicio de Android
    game_log("Llamando JNI_OnLoad...\n");
    if (Game_JNI_OnLoad)
      Game_JNI_OnLoad(&jvm, NULL);
    game_log("Llamando NativeInit...\n");
    if (NativeInit)
      NativeInit(jniEnv, NULL);
#ifndef HIDE_VIRTUAL_BUTTONS
    if (SetDpadDrawingBlock)
      SetDpadDrawingBlock(jniEnv, NULL, 0);
    if (SetDpadPosition)
      SetDpadPosition(jniEnv, NULL, 4, 123, 1);
    if (SetButtonPosition)
      SetButtonPosition(jniEnv, NULL, 325, 163, 1);
    game_log("Botones virtuales inicializados: DpadPos=%p BtnPos=%p ShowDpad=%p\n",
             (void *)SetDpadPosition, (void *)SetButtonPosition,
             (void *)SetShowDirectionButton);
#endif
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

    SceCtrlData pad;
    SceTouchData touch;
    int last_touch = 0;
    int last_tx = 0, last_ty = 0;
    int active_touch_key = 0;
    unsigned int old_buttons = 0;
    int frame = 0;
    int last_ui_status = -1;

    while (1) {
      if (g_ui_status != last_ui_status) {
        last_ui_status = g_ui_status;
        update_virtual_buttons(g_ui_status);
      }
      sceCtrlPeekBufferPositive(0, &pad, 1);
      sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

      if ((frame++ % 120) == 0) {
        game_log("frame %d alive, touch.reportNum=%d pad.buttons=0x%08x "
                 "ui_status=%d\n",
                 frame, touch.reportNum, (unsigned int)pad.buttons,
                 g_ui_status);
      }

      // Salida de emergencia: START+SELECT juntos (START solo ya no,
      // para poder usarlo como tecla del juego mas adelante)
      if ((pad.buttons & SCE_CTRL_START) && (pad.buttons & SCE_CTRL_SELECT))
        break;

      // --- Botones fisicos -> teclas HAL (press/release por flanco) ---
      unsigned int pressed = pad.buttons & ~old_buttons;
      unsigned int released = old_buttons & ~pad.buttons;
      for (int i = 0; i < BTN_MAP_COUNT; i++) {
        if (pressed & btn_map[i].btn)
          queue_input_event(jniEnv, MH_KEY_PRESSEVENT, btn_map[i].hal, 0);
        if (released & btn_map[i].btn)
          queue_input_event(jniEnv, MH_KEY_RELEASEEVENT, btn_map[i].hal, 0);
      }
      old_buttons = pad.buttons;

      // --- Touch: panel 1920x1088 -> espacio del juego 400x240 ---
      if (touch.reportNum > 0) {
        int x = touch.report[0].x * 400 / 1920;
        int y = touch.report[0].y * 240 / 1088;
        last_tx = x;
        last_ty = y;

        if (!last_touch) {
#ifndef HIDE_VIRTUAL_BUTTONS
          if (g_ui_status >= 3) {
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
#else
          queue_input_event(jniEnv, MH_POINTER_PRESSEVENT, x, y);
#endif
          last_touch = 1;
        }
      } else if (last_touch) {
#ifndef HIDE_VIRTUAL_BUTTONS
        if (active_touch_key != 0) {
          queue_input_event(jniEnv, MH_KEY_RELEASEEVENT, active_touch_key, 0);
          active_touch_key = 0;
        } else {
          queue_input_event(jniEnv, MH_POINTER_RELEASEEVENT, last_tx, last_ty);
        }
#else
        queue_input_event(jniEnv, MH_POINTER_RELEASEEVENT, last_tx, last_ty);
#endif
        last_touch = 0;
      }

      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      // Igual que NexusGLRenderer.drawFrame: entregar el evento clet
      // pendiente (uno por frame) justo antes de NativeRender
      if (handleCletEvent && eq_head != eq_tail) {
        input_event *ev = &event_queue[eq_head];
        eq_head = (eq_head + 1) % 16;
        handleCletEvent(jniEnv, NULL, ev->type, ev->p1, ev->p2);
      }

      // Renderizar el frame
      if (NativeRender)
        NativeRender(jniEnv, NULL);

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