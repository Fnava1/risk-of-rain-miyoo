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
    if (!src || bytes < 4) return 0;

    // Enforce 4-byte frame alignment (16-bit stereo: 2 channels * 2 bytes = 4 bytes)
    bytes &= ~3ULL;

    pthread_mutex_lock(&s_audio_lock);
    size_t space = AUDIO_RING_BUFFER_SIZE - s_available;
    space &= ~3ULL;

    if (bytes > space) {
        bytes = space;
    }

    if (bytes == 0) {
        pthread_mutex_unlock(&s_audio_lock);
        return 0;
    }

    const uint8_t *in = (const uint8_t *)src;
    size_t first_chunk = AUDIO_RING_BUFFER_SIZE - s_write_pos;
    if (bytes <= first_chunk) {
        memcpy(s_ring_buffer + s_write_pos, in, bytes);
        s_write_pos = (s_write_pos + bytes) % AUDIO_RING_BUFFER_SIZE;
    } else {
        memcpy(s_ring_buffer + s_write_pos, in, first_chunk);
        memcpy(s_ring_buffer, in + first_chunk, bytes - first_chunk);
        s_write_pos = bytes - first_chunk;
    }

    s_available += bytes;
    pthread_mutex_unlock(&s_audio_lock);
    return bytes;
}

size_t audio_stream_read_pcm(void *dst, size_t bytes) {
    if (!dst || bytes < 4) return 0;

    bytes &= ~3ULL;

    pthread_mutex_lock(&s_audio_lock);
    if (bytes > s_available) {
        bytes = s_available & ~3ULL;
    }

    if (bytes == 0) {
        pthread_mutex_unlock(&s_audio_lock);
        return 0;
    }

    uint8_t *out = (uint8_t *)dst;
    size_t first_chunk = AUDIO_RING_BUFFER_SIZE - s_read_pos;
    if (bytes <= first_chunk) {
        memcpy(out, s_ring_buffer + s_read_pos, bytes);
        s_read_pos = (s_read_pos + bytes) % AUDIO_RING_BUFFER_SIZE;
    } else {
        memcpy(out, s_ring_buffer + s_read_pos, first_chunk);
        memcpy(out + first_chunk, s_ring_buffer, bytes - first_chunk);
        s_read_pos = bytes - first_chunk;
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
    pthread_mutex_lock(&s_audio_lock);
    size_t avail = s_available;
    pthread_mutex_unlock(&s_audio_lock);

    // If buffer is low, synthesize or stream next audio chunk
    if (avail < 32768) {
        int16_t dummy_chunk[256];
        memset(dummy_chunk, 0, sizeof(dummy_chunk));
        audio_stream_write_pcm(dummy_chunk, sizeof(dummy_chunk));
    }
}

void audio_sdl_callback(void *userdata, uint8_t *stream, int len) {
    (void)userdata;
    size_t read = audio_stream_read_pcm(stream, (size_t)len);
    if (read < (size_t)len) {
        memset(stream + read, 0, (size_t)len - read);
    }
}
