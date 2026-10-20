#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <assert.h>

#include "src/platform/mmp_video.h"

int main() {
    printf("[*] Running Frame Pacing & Video Driver Unit Test...\n");

    const int total_frames = 60;
    const uint64_t target_frame_time_ns = 16666667ULL; // 60 FPS (16.666 ms)
    double frame_times_ms[total_frames];

    struct timespec start, end;
    for (int i = 0; i < total_frames; i++) {
        clock_gettime(CLOCK_MONOTONIC, &start);

        // Simulate 8ms of game logic
        struct timespec sim_work = {0, 8000000L};
        nanosleep(&sim_work, NULL);

        // Run the monotonic frame pacing function
        mmp_frame_pacing(target_frame_time_ns);

        clock_gettime(CLOCK_MONOTONIC, &end);
        uint64_t elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000ULL + (end.tv_nsec - start.tv_nsec);
        frame_times_ms[i] = elapsed_ns / 1000000.0;
    }

    double sum = 0.0;
    for (int i = 0; i < total_frames; i++) {
        sum += frame_times_ms[i];
    }
    double mean = sum / total_frames;

    double variance = 0.0;
    for (int i = 0; i < total_frames; i++) {
        variance += (frame_times_ms[i] - mean) * (frame_times_ms[i] - mean);
    }
    double stddev = sqrt(variance / total_frames);

    printf("  Mean Frame Time: %.2f ms (Target: 16.67 ms)\n", mean);
    printf("  Standard Deviation (Jitter): %.3f ms\n", stddev);

    // Frame time must be within 16.67 +/- 1.0 ms and stddev < 1.5 ms (accommodating QEMU emulation jitter)
    if (fabs(mean - 16.67) > 1.0) {
        printf("FAIL: Mean frame time drifted too much (%.2f ms)\n", mean);
        return 1;
    }
    if (stddev > 1.5) {
        printf("FAIL: Frame pacing jitter is too high (%.3f ms)\n", stddev);
        return 1;
    }

    // Test 180-degree coordinate inversion mapping
    int x = 10, y = 20;
    int inv_x, inv_y;
    mmp_invert_coords_180(x, y, 640, 480, &inv_x, &inv_y);
    assert(inv_x == (639 - x));
    assert(inv_y == (479 - y));

    printf("[+] Frame pacing test PASSED.\n");
    return 0;
}
