#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "src/audio/audio_tremor.h"

int main() {
    printf("[*] Running Audio Ring Buffer Alignment & Bulk Copy Fix Test...\n");

    audio_stream_init();

    // 1. Test unaligned byte write (5 bytes passed, only 4 bytes = 1 stereo frame should be accepted)
    uint8_t unaligned_data[5] = { 1, 2, 3, 4, 5 };
    size_t written = audio_stream_write_pcm(unaligned_data, 5);
    assert(written == 4);
    assert(audio_stream_available_bytes() == 4);

    audio_stream_shutdown();
    printf("[+] Audio alignment test PASSED.\n");
    return 0;
}
