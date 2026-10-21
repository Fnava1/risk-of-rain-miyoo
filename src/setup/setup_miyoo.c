#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "fb_progress.h"

#ifdef _WIN32
#include <io.h>
#define F_OK 0
#define access _access
#define sleep(s)
#define sync()
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#endif

// Known Risk of Rain audio tracks (exact case required on Linux)
static const char *s_known_ogg[] = {
    "musicStage1.ogg", "musicStage2.ogg", "musicStage3.ogg", "musicStage4.ogg",
    "musicStage5.ogg", "musicStage6.ogg", "musicStage7.ogg", "musicStage8.ogg",
    "musicStage9.ogg", "musicStage10.ogg", "musicStage11.ogg",
    "musicBoss1a.ogg", "musicBoss1b.ogg", "musicBoss1c.ogg",
    "musicBoss2a.ogg", "musicBoss2b.ogg", "musicBoss2c.ogg",
    "musicBoss3.ogg", "HitItSon.ogg", "wChefShoot2_1.ogg",
    NULL
};

// Search for FORM chunk inside a PE32 Windows executable and extract data.win
static bool extract_data_win_from_exe(const char *exe_path, const char *out_win_path) {
    FILE *f_in = fopen(exe_path, "rb");
    if (!f_in) return false;

    // Scan backwards from end or forwards in blocks
    fseek(f_in, 0, SEEK_END);
    long file_sz = ftell(f_in);
    fseek(f_in, 0, SEEK_SET);

    const size_t buf_sz = 64 * 1024;
    uint8_t *buf = (uint8_t *)malloc(buf_sz);
    if (!buf) {
        fclose(f_in);
        return false;
    }

    long form_offset = -1;
    uint32_t form_len = 0;
    long curr_pos = 0;

    while (curr_pos < file_sz) {
        fseek(f_in, curr_pos, SEEK_SET);
        size_t n = fread(buf, 1, buf_sz, f_in);
        if (n < 8) break;

        for (size_t i = 0; i <= n - 8; i++) {
            if (buf[i] == 'F' && buf[i+1] == 'O' && buf[i+2] == 'R' && buf[i+3] == 'M') {
                uint32_t sz = (uint32_t)buf[i+4] | ((uint32_t)buf[i+5] << 8) |
                              ((uint32_t)buf[i+6] << 16) | ((uint32_t)buf[i+7] << 24);
                // GameMaker Studio FORM chunk is typically > 10 MB
                if (sz > 5 * 1024 * 1024 && (curr_pos + (long)i + 8 + sz) <= file_sz + 4) {
                    form_offset = curr_pos + (long)i;
                    form_len = sz;
                    break;
                }
            }
        }
        if (form_offset >= 0) break;
        curr_pos += (long)(buf_sz - 8);
    }

    if (form_offset < 0) {
        free(buf);
        fclose(f_in);
        return false;
    }

    printf("[SETUP] Found embedded GameMaker FORM payload at offset 0x%lx (%u bytes)\n", form_offset, form_len);

    FILE *f_out = fopen(out_win_path, "wb");
    if (!f_out) {
        free(buf);
        fclose(f_in);
        return false;
    }

    fseek(f_in, form_offset, SEEK_SET);
    size_t total_to_write = (size_t)form_len + 8;
    size_t written = 0;

    while (written < total_to_write) {
        size_t chunk = (total_to_write - written > buf_sz) ? buf_sz : (total_to_write - written);
        size_t r = fread(buf, 1, chunk, f_in);
        if (r == 0) break;
        fwrite(buf, 1, r, f_out);
        written += r;
        fb_progress_update(0.1f + 0.35f * ((float)written / (float)total_to_write), "Extrayendo data.win de ejecutable...");
    }

    fclose(f_out);
    free(buf);
    fclose(f_in);

    return (written >= total_to_write);
}

// Normalize uppercase .ogg filenames to match GameMaker Linux casing
static void normalize_ogg_casing(void) {
#ifndef _WIN32
    DIR *d = opendir(".");
    if (!d) return;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type != DT_REG && dir->d_type != DT_UNKNOWN) continue;
        const char *name = dir->d_name;
        size_t len = strlen(name);
        if (len < 5) continue;

        // Check if ends in .ogg (case-insensitive)
        if (strcasecmp(name + len - 4, ".ogg") == 0) {
            for (int i = 0; s_known_ogg[i] != NULL; i++) {
                if (strcasecmp(name, s_known_ogg[i]) == 0 && strcmp(name, s_known_ogg[i]) != 0) {
                    printf("[SETUP] Renaming '%s' -> '%s' (case fix)\n", name, s_known_ogg[i]);
                    rename(name, s_known_ogg[i]);
                    break;
                }
            }
        }
    }
    closedir(d);
#endif
}

// Ensure clean Prefs.ini with Low graphics and Gamepad enabled
static void ensure_clean_prefs_ini(void) {
    if (access("Prefs.ini", F_OK) != 0) {
        printf("[SETUP] Creating default Prefs.ini (Low graphics, Gamepad enabled)...\n");
        FILE *f = fopen("Prefs.ini", "w");
        if (f) {
            fprintf(f, "[Video Options]\n");
            fprintf(f, "graphics=1.0\n");
            fprintf(f, "scale=1.0\n");
            fprintf(f, "fullscreen=0.0\n");
            fprintf(f, "\n");
            fprintf(f, "[Controls (Gamepad)]\n");
            fprintf(f, "joystick_actually_on=1.0\n");
            fprintf(f, "joystick_on=1.0\n");
            fclose(f);
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("=====================================================\n");
    printf(" Risk of Rain (2013) — On-Device Miyoo Asset Setup\n");
    printf("=====================================================\n");

    // 1. If already installed and data.win exists, skip immediately
    if (access(".installed", F_OK) == 0) {
        if (access("data_mmp.win", F_OK) == 0 || access("data.win", F_OK) == 0) {
            printf("[SETUP] Assets already installed and verified. Skipping setup.\n");
            return 0;
        }
        // Marker existed but data was removed: clean orphan marker
        remove(".installed");
    }

    // 2. Initialize Framebuffer UI on /dev/fb0
    fb_progress_init();
    fb_progress_update(0.05f, "Verificando archivos de instalacion...");

    // 3. Check for data_mmp.win or data.win
    bool has_data = (access("data_mmp.win", F_OK) == 0 || access("data.win", F_OK) == 0);

    if (!has_data) {
        // Check for Windows executable to extract from
        const char *exe_candidates[] = {
            "Risk of Rain.exe", "RiskofRain.exe", "Risk of Rain.EXE", "riskofrain.exe", NULL
        };

        const char *found_exe = NULL;
        for (int i = 0; exe_candidates[i] != NULL; i++) {
            if (access(exe_candidates[i], F_OK) == 0) {
                found_exe = exe_candidates[i];
                break;
            }
        }

        if (found_exe) {
            printf("[SETUP] Found Windows executable '%s'. Extracting data.win...\n", found_exe);
            fb_progress_update(0.15f, "Extrayendo data.win de Risk of Rain.exe...");
            if (extract_data_win_from_exe(found_exe, "data.win")) {
                printf("[SETUP] Successfully extracted data.win from '%s'!\n", found_exe);
                has_data = true;
            } else {
                fprintf(stderr, "[SETUP] Failed to extract data.win from '%s'.\n", found_exe);
            }
        }
    }

    // 4. If still no data.win found, show friendly modal error
    if (!has_data) {
        fb_progress_show_error(
            "ARCHIVOS NO ENCONTRADOS",
            "No se encontro data.win ni Risk of Rain.exe.",
            "Copia tus archivos de Steam a esta carpeta.",
            "Regresando al menu de OnionOS..."
        );
        fprintf(stderr, "[SETUP] Error: Neither data.win nor Risk of Rain.exe found!\n");
#ifndef _WIN32
        sleep(5);
#endif
        fb_progress_close();
        return 1;
    }

    // 5. Verify and normalize audio files
    fb_progress_update(0.55f, "Comprobando y normalizando musica (.ogg)...");
    normalize_ogg_casing();

    // 6. Ensure optimal Prefs.ini
    fb_progress_update(0.80f, "Configurando preferencias (Low, Gamepad)...");
    ensure_clean_prefs_ini();

    // 7. Create installation transaction marker
    fb_progress_update(0.95f, "Finalizando instalacion...");
    FILE *f_inst = fopen(".installed", "w");
    if (f_inst) {
        fprintf(f_inst, "Risk of Rain (2013) Miyoo Mini Plus - Setup Complete\n");
        fclose(f_inst);
    }

    sync();

    fb_progress_update(1.0f, "Instalacion completada con exito!");
#ifndef _WIN32
    usleep(500000); // 0.5s pause to show 100%
#endif

    fb_progress_close();
    printf("[SETUP] Setup completed successfully. Ready to launch Risk of Rain!\n");

    return 0;
}
