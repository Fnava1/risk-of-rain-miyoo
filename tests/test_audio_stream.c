#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "src/audio/audio_tremor.h"

int main() {
    printf("[*] Running Audio Tremor Streaming & Ring-Buffer Test...\n");

    // 1. Initialize audio streaming ring buffer (128 KB)
    audio_stream_init();
    assert(audio_stream_get_buffer_size() == 131072);

    // 2. Feed simulated PCM samples into ring buffer
    const int sample_count = 1024;
    int16_t test_pcm[sample_count];
    for (int i = 0; i < sample_count; i++) {
        test_pcm[i] = (int16_t)(i * 30);
    }

    size_t written = audio_stream_write_pcm(test_pcm, sample_count * sizeof(int16_t));
    assert(written == sample_count * sizeof(int16_t));
    assert(audio_stream_available_bytes() == sample_count * sizeof(int16_t));

    // 3. Read back PCM samples via simulated audio callback
    int16_t read_pcm[sample_count];
    size_t read_bytes = audio_stream_read_pcm(read_pcm, sample_count * sizeof(int16_t));
    assert(read_bytes == sample_count * sizeof(int16_t));
    assert(memcmp(test_pcm, read_pcm, sample_count * sizeof(int16_t)) == 0);
    assert(audio_stream_available_bytes() == 0);

    // 4. Test 3ms de-click fade out
    int16_t click_pcm[64];
    for (int i = 0; i < 64; i++) click_pcm[i] = 10000;
    audio_stream_apply_declick(click_pcm, 64);
    // Last sample must fade down smoothly toward 0
    assert(click_pcm[63] < click_pcm[0]);

    audio_stream_shutdown();
    printf("[+] Audio Tremor Streaming test PASSED.\n");
    return 0;
}
