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

#define printf psvDebugScreenPrintf

FILE *log_file = NULL;

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

    printf("%s", string);
    if (log_file) {
        fprintf(log_file, "%s", string);
        fflush(log_file);
    }
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

// Dummy JNI function to catch unknown JNI calls
int dummy_jni_func() {
    game_log("[FakeJNI] Dummy JNI function called!\n");
    return 0;
}

int fake_GetEnv(void *vm, void **env, int version) {
    game_log("[FakeJNI] GetEnv called!\n");
    extern void* fake_env_ptr;
    *env = &fake_env_ptr;
    return 0;
}

int fake_AttachCurrentThread(void *vm, void **env, void *args) {
    game_log("[FakeJNI] AttachCurrentThread called!\n");
    extern void* fake_env_ptr;
    *env = &fake_env_ptr;
    return 0;
}

extern void* fake_env_vtable[300];
void* fake_env_ptr = fake_env_vtable;

void* fake_vm_vtable[300];
void* fake_vm_ptr = fake_vm_vtable;

// Game JNI Pointers
int (* JNI_OnLoad)(void *vm, void *reserved);
void (* NativeInit)(void *env, void *obj);
void (* NativeRender)(void *env, void *obj);
void (* NativeResize)(void *env, void *obj, int w, int h);
void (* setInputEvent)(void *env, void *obj, int type, int p1, int p2);

int main() {
    for (int i = 0; i < 300; ++i) {
        fake_vm_vtable[i] = (void*)dummy_jni_func;
    }

    fake_vm_vtable[4] = (void*)fake_AttachCurrentThread;
    fake_vm_vtable[6] = (void*)fake_GetEnv;
    fake_vm_vtable[7] = (void*)fake_AttachCurrentThread;

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

		// Relocalizacion y Resolucion de dependencias
		so_relocate(&zenonia2_mod);
		so_resolve(&zenonia2_mod, default_dynlib, default_dynlib_size, 0);

		// Inicializar
		so_flush_caches(&zenonia2_mod);
		so_initialize(&zenonia2_mod);
		
		printf("SoLoader inicializado. Iniciando vitaGL...\n");

		// Iniciar vitaGL
		// vglInit(0); // TEMPORARILY DISABLED to avoid Vita3K SceShaccCg crash

		// Obtener punteros de funciones JNI
		JNI_OnLoad = (void *)so_symbol(&zenonia2_mod, "JNI_OnLoad");
		NativeInit = (void *)so_symbol(&zenonia2_mod, "Java_com_gamevil_nexus2_Natives_NativeInit");
		NativeRender = (void *)so_symbol(&zenonia2_mod, "Java_com_gamevil_nexus2_Natives_NativeRender");
		NativeResize = (void *)so_symbol(&zenonia2_mod, "Java_com_gamevil_nexus2_Natives_NativeResize");
		setInputEvent = (void *)so_symbol(&zenonia2_mod, "Java_com_gamevil_nexus2_Natives_setInputEvent");

		// Ejecutar la secuencia de inicio de Android
		game_log("Llamando JNI_OnLoad...\n");
		if (JNI_OnLoad) JNI_OnLoad(&fake_vm_ptr, NULL);
		game_log("Llamando NativeInit...\n");
		if (NativeInit) NativeInit(&fake_env_ptr, NULL);
		game_log("Llamando NativeResize...\n");
		if (NativeResize) NativeResize(&fake_env_ptr, NULL, 960, 544);

		printf("Iniciando Bucle Principal...\n");

		// Habilitar muestreo táctil
		sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

		SceCtrlData pad;
		SceTouchData touch;
		int last_touch = 0;

		while (1) {
			sceCtrlPeekBufferPositive(0, &pad, 1);
			sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

			if (pad.buttons & SCE_CTRL_START) break; // Salida de emergencia

			// --- Mapeo Táctil ---
			if (setInputEvent) {
				if (touch.reportNum > 0) {
					int x = touch.report[0].x * 960 / 1920; // Normalizar coordenadas táctiles Vita -> 960x544
					int y = touch.report[0].y * 544 / 1088;
					
					if (!last_touch) { // Touch Down
						setInputEvent(&fake_env_ptr, NULL, 0, x, y); // Tipo 0 suele ser Down
						last_touch = 1;
					} else { // Touch Move
						setInputEvent(&fake_env_ptr, NULL, 2, x, y); // Tipo 2 Move
					}
				} else if (last_touch) { // Touch Up
					setInputEvent(&fake_env_ptr, NULL, 1, 0, 0); // Tipo 1 Up
					last_touch = 0;
				}
			}

			// Renderizar el frame
			if (NativeRender) NativeRender(&fake_env_ptr, NULL);

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