#ifndef __AUDIO_H__
#define __AUDIO_H__

/**
 * @brief Reproductor de audio del port: replica los 3 canales del NexusSound.
 * @note Ver docs/loader/audio.md para el razonamiento de diseño.
 */

void audio_init(void);
void audio_play(int snd_id, int vol, int is_loop); // OnSoundPlay(id, vol, isLoop)
void audio_stop_all(void);                         // OnStopSound

#endif
