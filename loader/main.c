/*
 * main.c
 *
 * ARMv7 Shared Libraries loader. Zenonia 2.
 */

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <stdarg.h>

#include "debugScreen.h"
#include "so_util.h"
#include <taihen.h>
#include <vitaGL.h>
#include <falso_jni/FalsoJNI.h>

#define printf psvDebugScreenPrintf

FILE *log_file = NULL;

// Once vitaGL owns the display, debugScreen's raw framebuffer writes must not
// keep running alongside it -- both would fight over the same framebuffer.
int gl_active = 0;

int _newlib_heap_size_user = 128 * 1024 * 1024; // 128 MB for newlib (malloc)
unsigned int sceLibcHeapSize = 128 * 1024 * 1024; // 128 MB for SCE Libc

void init_log() {
    log_file = fopen("ux0:data/zenonia-2/log.txt", "w");
    if (log_file) {
        fprintf(log_file, "--- ZENONIA 2 PORT LOG START ---\n");
        fflush(log_file);
    }
}

void game_log(const char *fmt, ...) {
    va_list list;
    char string[512];

    va_start(list, fmt);
    vsnprintf(string, sizeof(string), fmt, list);
    va_end(list);

    if (!gl_active) printf("%s", string);
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
int (* Game_JNI_OnLoad)(void *vm, void *reserved);
void (* NativeInit)(void *env, void *obj);
void (* NativeRender)(void *env, void *obj);
void (* NativeResize)(void *env, void *obj, int w, int h);
void (* setInputEvent)(void *env, void *obj, int type, int p1, int p2);

void gl_init() {
    // No MSAA / no triple buffering: this is the config known to work on
    // real hardware (see port_progress.md for the Vita3K investigation --
    // vitaGL init reliably crashes inside Vita3K's own call_import dispatcher
    // regardless of these settings, confirmed to be an emulator-session
    // instability rather than a port bug, so untested against real hardware
    // yet by this specific build).
    vglUseTripleBuffering(GL_FALSE);
    vglInitExtended(0, 960, 544, 6 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    gl_active = 1;
}

int main() {
    psvDebugScreenInit();
    init_log();
	game_log("Iniciando Zenonia 2 port (SoLoader)\n");

	// Cargar la libreria SO en memoria
	int res = so_file_load(&zenonia2_mod, "ux0:data/zenonia-2/libzenonia2.so", 0x98000000);
	if (res < 0) {
		game_log("Error critico cargando libzenonia2.so: 0x%08X\n", res);
        sceKernelDelayThread(5000000); // 5 segundos para que se pueda leer el error
	} else {
		game_log("Libreria cargada con exito.\n");
		game_log("mod: text_base=0x%08x num_dynsym=%d dynsym=%p dynstr=%p hash=%p soname=%s\n",
			(unsigned int) zenonia2_mod.text_base, zenonia2_mod.num_dynsym,
			(void*)zenonia2_mod.dynsym, (void*)zenonia2_mod.dynstr, (void*)zenonia2_mod.hash,
			zenonia2_mod.soname ? zenonia2_mod.soname : "(null)");

		// Relocalizacion y Resolucion de dependencias
		so_relocate(&zenonia2_mod);
		so_resolve(&zenonia2_mod, default_dynlib, default_dynlib_size, 0);

		// Inicializar
		so_flush_caches(&zenonia2_mod);
		so_initialize(&zenonia2_mod);

		game_log("SoLoader inicializado. Iniciando vitaGL...\n");
		gl_init();
		game_log("vitaGL inicializado.\n");

		jni_init();
		JNIEnv *jniEnv = &jni;

		// Obtener punteros de funciones JNI
		Game_JNI_OnLoad = (void *)so_symbol(&zenonia2_mod, "JNI_OnLoad");
		NativeInit = (void *)so_symbol(&zenonia2_mod, "Java_com_gamevil_nexus2_Natives_NativeInit");
		NativeRender = (void *)so_symbol(&zenonia2_mod, "Java_com_gamevil_nexus2_Natives_NativeRender");
		NativeResize = (void *)so_symbol(&zenonia2_mod, "Java_com_gamevil_nexus2_Natives_NativeResize");
		setInputEvent = (void *)so_symbol(&zenonia2_mod, "Java_com_gamevil_nexus2_Natives_setInputEvent");

		game_log("Symbols: JNI_OnLoad=%p NativeInit=%p NativeRender=%p NativeResize=%p setInputEvent=%p\n",
			(void*)Game_JNI_OnLoad, (void*)NativeInit, (void*)NativeRender, (void*)NativeResize, (void*)setInputEvent);

		// Ejecutar la secuencia de inicio de Android
		game_log("Llamando JNI_OnLoad...\n");
		if (Game_JNI_OnLoad) Game_JNI_OnLoad(&jvm, NULL);
		game_log("Llamando NativeInit...\n");
		if (NativeInit) NativeInit(jniEnv, NULL);
		game_log("Llamando NativeResize...\n");
		if (NativeResize) NativeResize(jniEnv, NULL, 960, 544);

		game_log("Iniciando Bucle Principal...\n");

		// Habilitar muestreo táctil
		sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

		SceCtrlData pad;
		SceTouchData touch;
		int last_touch = 0;
		int frame = 0;

		while (1) {
			sceCtrlPeekBufferPositive(0, &pad, 1);
			sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

			if ((frame++ % 120) == 0) {
				game_log("frame %d alive, touch.reportNum=%d pad.buttons=0x%08x\n", frame, touch.reportNum, (unsigned int) pad.buttons);
			}

			if (pad.buttons & SCE_CTRL_START) break; // Salida de emergencia

			// --- Mapeo Táctil ---
			if (setInputEvent) {
				if (touch.reportNum > 0) {
					int x = touch.report[0].x * 960 / 1920; // Normalizar coordenadas táctiles Vita -> 960x544
					int y = touch.report[0].y * 544 / 1088;

					if (!last_touch) { // Touch Down
						setInputEvent(jniEnv, NULL, 0, x, y); // Tipo 0 suele ser Down
						last_touch = 1;
					} else { // Touch Move
						setInputEvent(jniEnv, NULL, 2, x, y); // Tipo 2 Move
					}
				} else if (last_touch) { // Touch Up
					setInputEvent(jniEnv, NULL, 1, 0, 0); // Tipo 1 Up
					last_touch = 0;
				}
			}

			// Renderizar el frame
			if (NativeRender) NativeRender(jniEnv, NULL);

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