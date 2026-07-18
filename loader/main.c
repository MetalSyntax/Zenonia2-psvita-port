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

// De kubridge.h (no se incluye entero: sus structs SceKernelAddrPair/etc.
// chocan con los del vitasdk actual; so_util.c ya lo enlaza igual)
extern int kuKernelCpuUnrestrictedMemcpy(void *dst, const void *src,
                                         SceSize len);

#define printf psvDebugScreenPrintf
#define LOG_DIR "ux0:data/zenonia-2/logs"

FILE *log_file = NULL;

// Once vitaGL owns the display, debugScreen's raw framebuffer writes must not
// keep running alongside it -- both would fight over the same framebuffer.
int gl_active = 0;

int _newlib_heap_size_user = 128 * 1024 * 1024; // 128 MB for newlib (malloc)
unsigned int sceLibcHeapSize =
    4 * 1024 * 1024; // 4 MB for SCE Libc (system libs)

// One log file per run, named with its start timestamp, inside logs/ --
// keeps a full history across test runs instead of overwriting the same
// log.txt every time (see psvita-porting skill's hardware_debugging.md).
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

// Solo a archivo: la consola de debug en pantalla ya no se usa durante el
// arranque normal (el usuario ve el splash de bg0 en su lugar, ver splash_*).
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
  // La pantalla de debug se inicializa recien aca: en un arranque sano no
  // se muestra nunca ningun texto por pantalla.
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

// --- Input: replica del protocolo del APK (NexusGLRenderer + UIFullTouch +
// Zenonia2UIControllerView, confirmado por decompilacion con jadx). Cada
// evento se entrega DOS veces, igual que en Java: setInputEvent() inmediato
// al generarse, y handleCletEvent() justo antes del siguiente NativeRender
// (NexusGLRenderer.drawFrame -> sendHandleCletEvent). El touch va en el
// espacio interno del juego de 400x240 (UIFullTouch.convertScreenX/Y), no en
// pixeles de pantalla.

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

// Botones fisicos de la Vita -> teclas HAL que en el telefono generaba la UI
// tactil de Java (dpad y botones en pantalla), que aca no existe.
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

// --- Parches binarios al .so (aplicar despues de so_relocate/so_resolve y
// ANTES de so_flush_caches, que sincroniza la cache de instrucciones) ---
//
// CMvLayerData::PreLoad+0x20 (VA 0xaec38): `cmp r3, #0; ble <skip>` donde r3
// es el PUNTERO al buffer del mapa pasado como `long` (con signo). En Android
// el heap vive en direcciones bajas (positivas) y el chequeo pasa; en Vita
// nuestro heap newlib esta en 0x81xxxxxx, negativo como entero con signo, asi
// que el motor "cree" que el buffer es invalido, saltea el calloc de las capas
// del mapa y CMvMap::CreateMiniMap crashea despues leyendo la capa NULL
// (Data abort confirmado con vita-parse-core: PC=CreateMiniMap+0xaa, R1=0).
// Se cambia `ble` (0xdd27) por `beq` (0xd027): solo saltear si es NULL real.
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
}

// --- Splash: logo.png/title.png/touch.png reales del APK (no el bg0 de
// LiveArea, que tiene el logo achicado y centrado sobre bordes negros
// pensados para la safe zone de LiveArea, no para pantalla completa) en
// pantalla hasta que el motor dibuje contenido real. Los estados 0 (logo
// Gamevil) y 1 (titulo) eran UI de Java en Android (aca se verian blancos);
// a partir del estado 2 el motor nativo ya dibuja el menu. En el estado 1,
// Android ademas parpadeaba touch.png ("toca para continuar", ver
// Zenonia2UIControllerView.showTouchViewAnim/TouchViewTimeTask en el APK
// decompilado) centrado horizontalmente a 3/4 de la pantalla -- sin ese
// aviso la pantalla de titulo se ve "trabada" hasta que el usuario prueba
// de tocar/apretar por su cuenta. g_ui_status lo actualiza java.c. ---
extern volatile int g_ui_status;

// logo.png/title.png/touch.png se leen tal cual vienen de
// apk_extract/res/drawable -- deployadas aparte por FTP (ver manage_vita.py)
// bajo ux0:data/zenonia-2/drawable/, no empaquetadas en el VPK -- y se
// decodifican/escalan en el dispositivo (ver image_load.c) en vez de leer
// los .rgba crudos pre-generados que este reemplaza.
#define DRAWABLE_DIR "ux0:data/zenonia-2/drawable"

static GLuint logo_tex = 0;
static GLuint title_tex = 0;
static GLuint touch_tex = 0;
// touch.png se escala por el mismo factor "cover" que title.png (800x480 ->
// 960x544, factor 1.2x) en vez de un cover-fit propio calculado de su
// aspecto, para que se vea consistente con el arte del titulo.
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

// Aviso "toca para continuar" sobre el titulo (estado 1), replicando la
// posicion original (centrado, topMargin = 3/4 de pantalla) y el parpadeo
// de Zenonia2UIControllerView vía un pulso de alpha en vez del fade casi
// imperceptible original (0.0 a 0.1 de alpha), para que se note en pantalla.
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

  // static: vitaGL no consume los vertex arrays de inmediato (los referencia
  // para el command buffer de GXM), asi que un array en el stack local aca
  // queda invalido para cuando efectivamente se dibuja -- eso generaba la
  // franja diagonal de colores basura reportada tras agregar este quad.
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
  // El motor deja GL_TEXTURE_ENV_MODE en GL_REPLACE una sola vez al iniciar
  // y nunca lo vuelve a tocar por frame (confirmado en el log: solo aparece
  // una vez al arrancar, nunca de nuevo en el bucle de dibujo del quad
  // compuesto 400x240). Bajo GL_REPLACE el color array por-vertice que el
  // motor deja armado (glColorPointer, no usado por REPLACE) es irrelevante;
  // si acá se deja en GL_MODULATE, ese color por-vertice (con valores no
  // pensados para modular nada) empieza a multiplicar la textura del motor
  // en TODOS los frames siguientes -- eso era la franja diagonal roja/verde
  // reportada en menu y juego, no un problema de los vertex arrays.
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
}

// Logs whatever sceDisplayGetFrameBuf currently reports as the buffer being
// scanned out, so we can tell from the log alone (without relying on what's
// visible on the TV/screen) whether vitaGL's swaps are actually taking over the
// display from debugScreen's own buffer.
void log_active_frame_buf(const char *label) {
  SceDisplayFrameBuf fb;
  memset(&fb, 0, sizeof(fb));
  fb.size = sizeof(fb);
  int ret = sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
  game_log("[DISPLAY] %s: sceDisplayGetFrameBuf ret=0x%08x base=%p w=%d h=%d "
           "pitch=%d\n",
           label, ret, fb.base, fb.width, fb.height, fb.pitch);
}

void gl_init() {
  // No MSAA / no triple buffering: this is the config known to work on
  // real hardware (see port_progress.md for the Vita3K investigation --
  // vitaGL init reliably crashes inside Vita3K's own call_import dispatcher
  // regardless of these settings, confirmed to be an emulator-session
  // instability rather than a port bug, so untested against real hardware
  // yet by this specific build).
  vglUseTripleBuffering(GL_FALSE);
  // vglInitExtended's return value is GL_TRUE only if the requested resolution
  // had to be reduced to fit the display's max (res_fallback in vitaGL's own
  // source) -- it is NOT a success/failure code, so GL_FALSE here (960x544 is
  // the Vita's native resolution, never falls back) is the expected, healthy
  // result. Do not treat it as an init failure.
  vglInitExtended(0, 960, 544, 6 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);

  // Cap a ~30 FPS con VSync real (sin tearing): la Vita refresca a ~59.94Hz,
  // asi que interval=2 espera 2 vblanks por swap en vez de 1 (que daria un
  // cap a ~60 FPS via vglWaitVblankStart). Sin este cap, con el compositor
  // por-software corriendo mas rapido que antes (boost de clocks Fase 15 +
  // conversion RGB565 optimizada Fase 16.1), el motor llegaba a ~40 FPS
  // sostenidos -- mas rapido que el ritmo original (30 FPS, hardware Android
  // de 2011) para el que esta calibrada la logica de juego, y sin VSync
  // (tearing visible en el blit del compositor).
  eglSwapInterval(eglGetDisplay(EGL_DEFAULT_DISPLAY), 2);

  gl_active = 1;
}

int main() {
  // Subir los clocks al maximo permitido por el firmware -- por defecto la
  // Vita corre a 333MHz de CPU / 111MHz de bus / 166MHz de GPU. El motor
  // hace bastante trabajo por-software (compositor 400x240, mixer de audio,
  // parsing JNI), asi que este es el mismo boost estandar que usan
  // practicamente todos los homebrews/ports (PPSSPP, etc.) y no tiene
  // downside conocido en hardware real (solo mas consumo/calor).
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

    game_log(
        "Symbols: JNI_OnLoad=%p NativeInit=%p NativeRender=%p NativeResize=%p "
        "setInputEvent=%p NativeResumeClet=%p handleCletEvent=%p\n",
        (void *)Game_JNI_OnLoad, (void *)NativeInit, (void *)NativeRender,
        (void *)NativeResize, (void *)setInputEvent, (void *)NativeResumeClet,
        (void *)handleCletEvent);

    // Ejecutar la secuencia de inicio de Android
    game_log("Llamando JNI_OnLoad...\n");
    if (Game_JNI_OnLoad)
      Game_JNI_OnLoad(&jvm, NULL);
    game_log("Llamando NativeInit...\n");
    if (NativeInit)
      NativeInit(jniEnv, NULL);
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
    unsigned int old_buttons = 0;
    int frame = 0;

    while (1) {
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
          queue_input_event(jniEnv, MH_POINTER_PRESSEVENT, x, y);
          last_touch = 1;
        }
        // UIFullTouch no manda eventos de move: solo press/release
      } else if (last_touch) {
        queue_input_event(jniEnv, MH_POINTER_RELEASEEVENT, last_tx, last_ty);
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

      // Mientras el motor este en logo (0) / titulo (1) -- pantallas que
      // eran UI de Java y aca se ven blancas -- tapar con el logo/titulo
      // reales del APK. A partir del estado 2 (menu) el motor dibuja de verdad.
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