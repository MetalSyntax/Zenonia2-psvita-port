# `loader/audio.h` — Documentación de diseño

Comentarios explicativos extraídos del código fuente y reemplazados por bloques Doxygen técnicos en el código. Este documento conserva el razonamiento (el "por qué") separado de la documentación técnica.

## `audio_init`

**Archivo:** `loader/audio.h`

> Reproductor de audio del port: replica los 3 canales del NexusSound.java de
> Android (SFX superpuestos via SoundPool, BGM en loop y stream one-shot via
> MediaPlayer) sobre sceAudioOut + Tremor (libvorbisidec).
>
> Los .ogg (sacados de apk_extract/res/raw/) deben estar en la consola en:
> ux0:data/zenonia-2/sound/sNNN.ogg   (ej. id 108 -> s108.ogg)

---
