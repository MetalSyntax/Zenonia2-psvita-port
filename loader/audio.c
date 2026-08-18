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

/**
 * @def NUM_VOICES
 * @brief Total de voces de mezcla: 1 BGM + 1 stream + 4 SFX simultáneos.
 * @note Ver docs/loader/audio.md para el razonamiento de diseño.
 */
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

/**
 * @struct play_request_t
 * @brief Pedido de reproducción de sonido pendiente de procesar.
 *
 * Encolado por audio_play() y drenado de forma asíncrona por audio_thread
 * (ver handle_play_request()).
 *
 * @var play_request_t::snd_id  ID de sonido (índice en sound/sNNN.ogg).
 * @var play_request_t::vol     Volumen 0-100.
 * @var play_request_t::is_loop No cero si debe reproducirse en loop.
 *
 * @note Ver docs/loader/audio.md para el razonamiento de diseño.
 */
typedef struct {
    int snd_id;
    int vol;
    int is_loop;
} play_request_t;

#define MAX_PENDING_REQUESTS 8
static play_request_t pending[MAX_PENDING_REQUESTS];
static int pending_head = 0;
static int pending_count = 0;

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

/**
 * @brief Abre, decodea el header y asigna a un canal una voz de reproducción.
 * @param req Pedido de reproducción a procesar.
 * @pre Debe ejecutarse únicamente en audio_thread, nunca en el hilo llamador
 *      de audio_play().
 * @note Ver docs/loader/audio.md para el razonamiento de diseño.
 */
static void handle_play_request(const play_request_t *req) {
    char path[128];
    snprintf(path, sizeof(path), SND_DIR "/s%03d.ogg", req->snd_id);

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

    /**
     * @brief Selección de voz destino según categoría (SFX/BGM/stream).
     * @note Replica la semántica de SoundPool/mBgmPlayer/mPlayer de
     *       NexusSound.java. Ver docs/loader/audio.md para el razonamiento.
     */
    voice_t *target = NULL;
    if (is_sfx_id(req->snd_id)) {
        for (int i = VOICE_SFX0; i < NUM_VOICES; i++)
            if (!voices[i].active) { target = &voices[i]; break; }
        if (!target) target = &voices[VOICE_SFX0];
    } else if (req->is_loop) {
        target = &voices[VOICE_BGM];
    } else {
        target = &voices[VOICE_STREAM];
    }

    voice_close(target);
    target->vf = vf;
    target->loop = req->is_loop;
    target->channels = vi->channels;
    /**
     * @brief Escala lineal de ganancia a partir de vol (0-100).
     * @note Ver docs/loader/audio.md para el razonamiento de diseño.
     */
    target->gain = req->vol > 0 ? (req->vol > 100 ? 1.0f : req->vol / 100.0f) : 1.0f;
    target->active = 1;

    sceKernelUnlockMutex(audio_mutex, 1);
}

static void drain_pending_requests(void) {
    for (;;) {
        play_request_t req;

        sceKernelLockMutex(audio_mutex, 1, NULL);
        if (pending_count == 0) {
            sceKernelUnlockMutex(audio_mutex, 1);
            break;
        }
        req = pending[pending_head];
        pending_head = (pending_head + 1) % MAX_PENDING_REQUESTS;
        pending_count--;
        sceKernelUnlockMutex(audio_mutex, 1);

        handle_play_request(&req);
    }
}

static int audio_thread(SceSize args, void *argp) {
    static int16_t mix[AUDIO_GRAIN * 2];
    static int16_t buf[AUDIO_GRAIN * 2];

    while (audio_running) {
        drain_pending_requests();

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

        /**
         * @brief Envía el bloque mezclado al hardware de audio.
         * @note Bloqueante: marca el ritmo del loop mezclador. Ver
         *       docs/loader/audio.md para el razonamiento de diseño.
         */
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

/**
 * @brief Encola un pedido de reproducción de sonido (OnSoundPlay).
 * @param snd_id  ID de sonido (índice en sound/sNNN.ogg).
 * @param vol     Volumen 0-100.
 * @param is_loop No cero si debe reproducirse en loop.
 * @pre Llamada desde el thread de render (ver java.c); debe ser rápida y
 *      jamás bloquear en I/O — solo encola, ver handle_play_request().
 * @note Ver docs/loader/audio.md para el razonamiento de diseño.
 */
void audio_play(int snd_id, int vol, int is_loop) {
    if (audio_port < 0) return;

    sceKernelLockMutex(audio_mutex, 1, NULL);
    if (pending_count < MAX_PENDING_REQUESTS) {
        int idx = (pending_head + pending_count) % MAX_PENDING_REQUESTS;
        pending[idx].snd_id = snd_id;
        pending[idx].vol = vol;
        pending[idx].is_loop = is_loop;
        pending_count++;
    }
    sceKernelUnlockMutex(audio_mutex, 1);
}

void audio_stop_all(void) {
    if (audio_port < 0) return;
    sceKernelLockMutex(audio_mutex, 1, NULL);
    for (int i = 0; i < NUM_VOICES; i++)
        voice_close(&voices[i]);
    sceKernelUnlockMutex(audio_mutex, 1);
}
