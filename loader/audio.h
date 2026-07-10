#ifndef __AUDIO_H__
#define __AUDIO_H__

// Reproductor de audio del port: replica los 3 canales del NexusSound.java de
// Android (SFX superpuestos via SoundPool, BGM en loop y stream one-shot via
// MediaPlayer) sobre sceAudioOut + Tremor (libvorbisidec).
//
// Los .ogg (sacados de apk_extract/res/raw/) deben estar en la consola en:
//   ux0:data/zenonia-2/sound/sNNN.ogg   (ej. id 108 -> s108.ogg)

void audio_init(void);
void audio_play(int snd_id, int vol, int is_loop); // OnSoundPlay(id, vol, isLoop)
void audio_stop_all(void);                         // OnStopSound

#endif
