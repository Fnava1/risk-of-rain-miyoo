#include <iostream>
#include <vector>
#include <csignal>
#include <atomic>

#include "src/platform/mmp_video.h"
#include "src/audio/audio_tremor.h"
#include "src/renderer/SoftwareRenderer.h"
#include "src/vm/GMLVM.h"

static std::atomic<bool> s_running{true};

void signal_handler(int sig) {
    (void)sig;
    s_running = false;
}

int main(int argc, char* argv[]) {
    std::cout << "=====================================================" << std::endl;
    std::cout << "  Risk of Rain (2013) - Miyoo Mini Plus Native Port  " << std::endl;
    std::cout << "  Platform: SigmaStar SSD202D / OnionOS               " << std::endl;
    std::cout << "=====================================================" << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 1. Initialize Framebuffer (640x480 native LCD)
    if (mmp_fb_init("/dev/fb0") < 0) {
        std::cerr << "[!] Note: Could not open /dev/fb0 (expected in emulator or host environment)" << std::endl;
    } else {
        std::cout << "[+] Framebuffer /dev/fb0 mapped successfully." << std::endl;
    }

    // 2. Initialize Audio Streaming Ring Buffer (128 KB)
    audio_stream_init();
    std::cout << "[+] Audio Tremor Ring-Buffer initialized (128 KB buffer)." << std::endl;

    // 3. Initialize GameMaker Studio 1.4 VM
    GMLVM vm;
    std::string save_file = vm.resolve_save_file_path("Save.ini");
    std::cout << "[+] Storage: Save file linked to " << save_file << std::endl;

    // Allocate backbuffer (640x480 RGB565)
    std::vector<uint16_t> backbuffer(640 * 480, 0x0000);

    // 4. Main Game Execution Loop
    std::cout << "[*] Entering 60 FPS Monotonic Game Loop..." << std::endl;
    const uint64_t target_frame_time_ns = 16666667ULL; // 60 FPS

    int frame_count = 0;
    while (s_running && frame_count < 60) {
        // A. Frame Pacing (sub-millisecond yield, no CONFIG_HZ=100 quantum drops)
        mmp_frame_pacing(target_frame_time_ns);

        // B. Audio pump to guarantee zero buffer underruns during rendering
        audio_pump();

        // C. Render / Flip to display with 180-degree hardware inverted mapping
        mmp_video_flip_180(backbuffer.data(), 640, 480);

        frame_count++;
    }

    // 5. Clean Exit & State Restoration
    std::cout << "[*] Restoring video & audio state..." << std::endl;
    mmp_fb_close();
    audio_stream_shutdown();

    std::cout << "[+] Exit cleanly returned to OnionOS." << std::endl;
    return 0;
}
