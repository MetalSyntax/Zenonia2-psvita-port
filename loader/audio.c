/*
 * audio.c
 *
 * Replica del pipeline de sonido del APK (NexusSound.java + SoundMgr.java,
 * decompilados con jadx -- ver port_progress.md §10.8):
 *
 *  - SoundMgr mapea el sndID de OnSoundPlay al recurso res/raw/sNNN.ogg y a un
 *    flag isSFX. Aca ese mapa se reduce a: ruta = sound/s%03d.ogg + tabla de
 *    IDs que son SFX.
 *  - isSFX  -> SoundPool en Android: one-shots que se superponen entre si.
 *  - !isSFX + isLoop  -> mBgmPlayer: musica de fondo, corta la anterior, loop.
 *  - !isSFX + !isLoop -> mPlayer: stream one-shot (jingles), corta el anterior.
 *  - OnStopSound -> stopAllSound().
 *
 * Implementacion Vita: un unico puerto BGM de sceAudioOut (22050 Hz estereo,
 * la frecuencia de todos los .ogg del juego) y un thread mezclador que decodea
 * con Tremor (vitasdk: libvorbisidec) cada voz activa y las suma con
 * saturacion. BGM/stream/SFX son todas voces del mismo mezclador; el caracter
 * de cada canal se respeta al despachar (que voz se reemplaza y cual se apila).
 */

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tremor/ivorbisfile.h>

#include "audio.h"

extern void game_log(const char *fmt, ...);

#define SND_DIR "ux0:data/zenonia-2/sound"

#define AUDIO_RATE 22050
#define AUDIO_GRAIN 512

// 1 BGM + 1 stream + 4 SFX simultaneos (SoundPool de Android se creaba con
// pocos streams; 4 alcanza de sobra para este juego)
#define VOICE_BGM    0
#define VOICE_STREAM 1
#define VOICE_SFX0   2
#define NUM_VOICES   6

// IDs que SoundMgr.java registra con isSFX=true
static const int sfx_ids[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    17, 18, 20, 22, 33, 34, 35, 36, 46, 47, 48, 67,
};

typedef struct {
    OggVorbis_File vf;
    int active;
    int loop;
    int channels;
    float gain;
} voice_t;

static voice_t voices[NUM_VOICES];
static SceUID audio_mutex = -1;
static SceUID audio_thread_id = -1;
static int audio_port = -1;
static volatile int audio_running = 0;

static int is_sfx_id(int id) {
    for (unsigned i = 0; i < sizeof(sfx_ids) / sizeof(sfx_ids[0]); i++)
        if (sfx_ids[i] == id) return 1;
    return 0;
}

static void voice_close(voice_t *v) {
    if (v->active) {
        ov_clear(&v->vf);
        v->active = 0;
    }
}

// Decodea hasta `frames` frames estereo de una voz en `out` (intercalado LR).
// Devuelve frames escritos; 0 = la voz termino (y ya fue cerrada).
static int voice_decode(voice_t *v, int16_t *out, int frames) {
    int done = 0;
    static int16_t tmp[AUDIO_GRAIN * 2];

    while (done < frames) {
        int want_frames = frames - done;
        int want_bytes = want_frames * v->channels * 2;
        if (want_bytes > (int) sizeof(tmp)) want_bytes = sizeof(tmp);

        int bs;
        long got = ov_read(&v->vf, (char *) tmp, want_bytes, &bs);
        if (got <= 0) {
            if (v->loop && got == 0 && ov_pcm_seek(&v->vf, 0) == 0)
                continue;
            voice_close(v);
            break;
        }

        int got_frames = (int) got / (v->channels * 2);
        for (int i = 0; i < got_frames; i++) {
            int16_t l = tmp[i * v->channels];
            int16_t r = tmp[i * v->channels + (v->channels > 1 ? 1 : 0)];
            out[(done + i) * 2] = (int16_t)(l * v->gain);
            out[(done + i) * 2 + 1] = (int16_t)(r * v->gain);
        }
        done += got_frames;
    }
    return done;
}

static int audio_thread(SceSize args, void *argp) {
    static int16_t mix[AUDIO_GRAIN * 2];
    static int16_t buf[AUDIO_GRAIN * 2];

    while (audio_running) {
        memset(mix, 0, sizeof(mix));

        sceKernelLockMutex(audio_mutex, 1, NULL);
        for (int vi = 0; vi < NUM_VOICES; vi++) {
            voice_t *v = &voices[vi];
            if (!v->active) continue;
            int got = voice_decode(v, buf, AUDIO_GRAIN);
            for (int i = 0; i < got * 2; i++) {
                int s = mix[i] + buf[i];
                if (s > 32767) s = 32767;
                if (s < -32768) s = -32768;
                mix[i] = (int16_t) s;
            }
        }
        sceKernelUnlockMutex(audio_mutex, 1);

        // Bloquea hasta que el hardware consumio el bloque: marca el ritmo del loop
        sceAudioOutOutput(audio_port, mix);
    }
    return 0;
}

void audio_init(void) {
    audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_GRAIN,
                                     AUDIO_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (audio_port < 0) {
        game_log("[AUDIO] sceAudioOutOpenPort fallo: 0x%08x\n", audio_port);
        return;
    }

    audio_mutex = sceKernelCreateMutex("zen2_audio_mutex", 0, 0, NULL);
    audio_running = 1;
    audio_thread_id = sceKernelCreateThread("zen2_audio", audio_thread,
                                            0x10000100, 0x10000, 0, 0, NULL);
    if (audio_thread_id >= 0) {
        sceKernelStartThread(audio_thread_id, 0, NULL);
        game_log("[AUDIO] mezclador iniciado (port=%d, %d Hz)\n", audio_port, AUDIO_RATE);
    }
}

void audio_play(int snd_id, int vol, int is_loop) {
    if (audio_port < 0) return;

    char path[128];
    snprintf(path, sizeof(path), SND_DIR "/s%03d.ogg", snd_id);

    FILE *f = fopen(path, "rb");
    if (!f) {
        static int miss_log = 0;
        if (miss_log < 20) {
            game_log("[AUDIO] no encontrado: %s\n", path);
            miss_log++;
        }
        return;
    }

    OggVorbis_File vf;
    if (ov_open(f, &vf, NULL, 0) < 0) {
        game_log("[AUDIO] ov_open fallo para %s\n", path);
        fclose(f);
        return;
    }
    vorbis_info *vi = ov_info(&vf, -1);
    if (!vi || (vi->channels != 1 && vi->channels != 2) || vi->rate != AUDIO_RATE) {
        game_log("[AUDIO] formato inesperado en %s (ch=%d rate=%ld)\n",
                 path, vi ? vi->channels : -1, vi ? vi->rate : -1);
        ov_clear(&vf); // tambien cierra el FILE*
        return;
    }

    sceKernelLockMutex(audio_mutex, 1, NULL);

    voice_t *target = NULL;
    if (is_sfx_id(snd_id)) {
        // SoundPool: buscar una voz SFX libre; si no hay, pisar la primera
        for (int i = VOICE_SFX0; i < NUM_VOICES; i++)
            if (!voices[i].active) { target = &voices[i]; break; }
        if (!target) target = &voices[VOICE_SFX0];
    } else if (is_loop) {
        target = &voices[VOICE_BGM];    // mBgmPlayer: corta la musica anterior
    } else {
        target = &voices[VOICE_STREAM]; // mPlayer: corta el stream anterior
    }

    voice_close(target);
    target->vf = vf;
    target->loop = is_loop;
    target->channels = vi->channels;
    // vol llega 0-100 (observado 50); MAX_VOLUME=80 en NexusSound. Escala lineal.
    target->gain = vol > 0 ? (vol > 100 ? 1.0f : vol / 100.0f) : 1.0f;
    target->active = 1;

    sceKernelUnlockMutex(audio_mutex, 1);
}

void audio_stop_all(void) {
    if (audio_port < 0) return;
    sceKernelLockMutex(audio_mutex, 1, NULL);
    for (int i = 0; i < NUM_VOICES; i++)
        voice_close(&voices[i]);
    sceKernelUnlockMutex(audio_mutex, 1);
}
