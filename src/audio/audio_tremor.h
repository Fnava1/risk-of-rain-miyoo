#ifndef AUDIO_TREMOR_H
#define AUDIO_TREMOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RING_BUFFER_SIZE (128 * 1024) // 128 KB fixed ring-buffer
#define AUDIO_SAMPLE_RATE 22050
#define AUDIO_CHANNELS 2

/**
 * Initializes the static 128 KB audio ring-buffer.
 */
void audio_stream_init(void);

/**
 * Shuts down the audio streaming subsystem and frees resources.
 */
void audio_stream_shutdown(void);

/**
 * Returns total capacity of the ring-buffer (131072 bytes).
 */
size_t audio_stream_get_buffer_size(void);

/**
 * Returns number of unread PCM bytes available in the ring-buffer.
 */
size_t audio_stream_available_bytes(void);

/**
 * Pushes raw PCM bytes into the ring-buffer.
 */
size_t audio_stream_write_pcm(const void *src, size_t bytes);

/**
 * Pulls PCM bytes from the ring-buffer (e.g. called by SDL Audio Callback).
 */
size_t audio_stream_read_pcm(void *dst, size_t bytes);

/**
 * Applies a 3ms linear anti-pop / de-click attenuation curve to samples.
 */
void audio_stream_apply_declick(int16_t *samples, int count);

/**
 * Periodic audio pump: refills the ring-buffer if capacity falls below threshold.
 * Invoked during rasterization spans to guarantee zero underruns.
 */
void audio_pump(void);

/**
 * SDL audio callback interface.
 */
void audio_sdl_callback(void *userdata, uint8_t *stream, int len);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_TREMOR_H
