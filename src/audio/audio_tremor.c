#include "audio_tremor.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static uint8_t s_ring_buffer[AUDIO_RING_BUFFER_SIZE];
static size_t s_read_pos = 0;
static size_t s_write_pos = 0;
static size_t s_available = 0;
static pthread_mutex_t s_audio_lock = PTHREAD_MUTEX_INITIALIZER;
static bool s_initialized = false;

void audio_stream_init(void) {
    pthread_mutex_lock(&s_audio_lock);
    memset(s_ring_buffer, 0, sizeof(s_ring_buffer));
    s_read_pos = 0;
    s_write_pos = 0;
    s_available = 0;
    s_initialized = true;
    pthread_mutex_unlock(&s_audio_lock);
}

void audio_stream_shutdown(void) {
    pthread_mutex_lock(&s_audio_lock);
    s_available = 0;
    s_initialized = false;
    pthread_mutex_unlock(&s_audio_lock);
}

size_t audio_stream_get_buffer_size(void) {
    return AUDIO_RING_BUFFER_SIZE;
}

size_t audio_stream_available_bytes(void) {
    pthread_mutex_lock(&s_audio_lock);
    size_t avail = s_available;
    pthread_mutex_unlock(&s_audio_lock);
    return avail;
}

size_t audio_stream_write_pcm(const void *src, size_t bytes) {
    if (!src || bytes == 0) return 0;

    pthread_mutex_lock(&s_audio_lock);
    size_t space = AUDIO_RING_BUFFER_SIZE - s_available;
    if (bytes > space) {
        bytes = space; // truncate to avoid overflow
    }

    const uint8_t *in = (const uint8_t *)src;
    for (size_t i = 0; i < bytes; i++) {
        s_ring_buffer[s_write_pos] = in[i];
        s_write_pos = (s_write_pos + 1) % AUDIO_RING_BUFFER_SIZE;
    }
    s_available += bytes;

    pthread_mutex_unlock(&s_audio_lock);
    return bytes;
}

size_t audio_stream_read_pcm(void *dst, size_t bytes) {
    if (!dst || bytes == 0) return 0;

    pthread_mutex_lock(&s_audio_lock);
    if (bytes > s_available) {
        bytes = s_available;
    }

    uint8_t *out = (uint8_t *)dst;
    for (size_t i = 0; i < bytes; i++) {
        out[i] = s_ring_buffer[s_read_pos];
        s_read_pos = (s_read_pos + 1) % AUDIO_RING_BUFFER_SIZE;
    }
    s_available -= bytes;

    pthread_mutex_unlock(&s_audio_lock);
    return bytes;
}

void audio_stream_apply_declick(int16_t *samples, int count) {
    if (!samples || count <= 0) return;

    for (int i = 0; i < count; i++) {
        // Fast fixed-point linear decay: (count - i) / count
        int32_t val = samples[i];
        samples[i] = (int16_t)((val * (count - i)) / count);
    }
}

void audio_pump(void) {
    // If available bytes fall below 32 KB, decode next chunk
    pthread_mutex_lock(&s_audio_lock);
    size_t avail = s_available;
    pthread_mutex_unlock(&s_audio_lock);

    if (avail < 32768) {
        // Placeholder for Tremor decoder feeding next uncompressed chunk from SD
    }
}

void audio_sdl_callback(void *userdata, uint8_t *stream, int len) {
    (void)userdata;
    size_t read = audio_stream_read_pcm(stream, (size_t)len);
    if (read < (size_t)len) {
        // Fill remaining underrun with silence (0) to prevent screeching noise
        memset(stream + read, 0, (size_t)len - read);
    }
}
