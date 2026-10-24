#include <loop.h>
#include <signal.h>
#include <unistd.h>
#include <execinfo.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "runner.h"
#include "vm.h"

/* For SDL_main */
#if defined(USE_SDL1)
#include <SDL/SDL_main.h>
#elif defined(USE_SDL2)
#include <SDL2/SDL_main.h>
#elif defined(USE_SDL3)
#include <SDL3/SDL_main.h>
#endif

static void miyoo_crash_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)ucontext;
    // Reset signals to default to prevent recursion if crash handler faults
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGFPE, SIG_DFL);
    signal(SIGILL, SIG_DFL);

    const char* signame = "UNKNOWN";
    switch (sig) {
        case SIGSEGV: signame = "SIGSEGV (Segmentation Fault)"; break;
        case SIGBUS:  signame = "SIGBUS (Bus Error)"; break;
        case SIGABRT: signame = "SIGABRT (Abort / Assertion Failure)"; break;
        case SIGFPE:  signame = "SIGFPE (Floating Point Exception)"; break;
        case SIGILL:  signame = "SIGILL (Illegal Instruction)"; break;
    }

    fprintf(stderr, "\n=======================================================\n");
    fprintf(stderr, "[FATAL CRASH] Caught signal %d: %s\n", sig, signame);
    if (info) {
        fprintf(stderr, "[FATAL CRASH] Faulting memory address: %p\n", info->si_addr);
    }

    if (g_active_runner != NULL) {
        Runner* r = g_active_runner;
        fprintf(stderr, "[FATAL CRASH] Engine Frame: %u, Room Index: %d\n", r->frameCount, r->currentRoomIndex);
        if (r->vmContext != NULL) {
            VMContext* ctx = r->vmContext;
            fprintf(stderr, "[FATAL CRASH] Active GML Script/Code: %s\n", ctx->currentCodeName ? ctx->currentCodeName : "(null)");
            fprintf(stderr, "[FATAL CRASH] Instruction offset: 0x%X (%u) / code length: %u\n", ctx->ip, ctx->ip, ctx->codeEnd);
            if (ctx->currentInstance != NULL) {
                fprintf(stderr, "[FATAL CRASH] Active Instance ID: %d, Object Index: %d\n",
                        ctx->currentInstance->instanceId, ctx->currentInstance->objectIndex);
            }
            if (ctx->otherInstance != NULL) {
                fprintf(stderr, "[FATAL CRASH] 'other' Instance ID: %d, Object Index: %d\n",
                        ctx->otherInstance->instanceId, ctx->otherInstance->objectIndex);
            }
            if (ctx->callDepth > 0 && ctx->callStack != NULL) {
                fprintf(stderr, "[FATAL CRASH] GML Call Stack (depth %d):\n", ctx->callDepth);
                CallFrame* frame = ctx->callStack;
                int depth = 0;
                while (frame != NULL && depth < 16) {
                    fprintf(stderr, "  #%d: %s at offset 0x%X (%u)\n",
                            depth++,
                            frame->savedCodeName ? frame->savedCodeName : "(unknown)",
                            frame->savedIP, frame->savedIP);
                    frame = frame->parent;
                }
            }
        }
    }

    fprintf(stderr, "[FATAL CRASH] Native C Backtrace:\n");
    void* callstack[32];
    int frames = backtrace(callstack, 32);
    backtrace_symbols_fd(callstack, frames, fileno(stderr));

    fprintf(stderr, "=======================================================\n");
    fflush(stderr);
    fflush(stdout);
    sync();

    // Terminate via the caught signal
    raise(sig);
}

static void install_crash_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = miyoo_crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
}

int main(int argc, char* argv[]) {
    (void)argc;
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    install_crash_handler();

    logInfo("[Runner] Butterscotch initializing with diagnostics & crash handler enabled\n");

    CommandLineArgs args = {0};

    args.exitAtFrame = -1;
#ifdef ENABLE_VM_TRACING
    args.traceBytecodeAfterFrame = 0;
#endif
    args.speedMultiplier = 1.0;
    args.fastForwardSpeed = 0.0;
    args.osType = OS_WINDOWS;
    args.profilerFramesBetween = 0;
    args.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;
    args.lazyRooms = true;
    args.lazyTextures = true;
    args.lazyAudio = true;
#if defined(ENABLE_MODERN_GL)
    args.renderer = MODERN_GL;
#elif defined(ENABLE_LEGACY_GL)
    args.renderer = LEGACY_GL;
#else
    args.renderer = SOFTWARE;
#endif
    args.dataWinPath = DATA_WIN_PATH;

    int ret = loop(args, argv[0]);
    freeCommandLineArgs(&args);
    return ret;
}
