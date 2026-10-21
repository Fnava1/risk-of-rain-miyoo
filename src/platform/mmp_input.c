#include "mmp_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

#define MAX_INPUT_DEVS 4

static int s_input_fds[MAX_INPUT_DEVS] = {-1, -1, -1, -1};
static int s_num_fds = 0;
static MMPInputState s_current_state;

int mmp_input_init(void) {
    memset(&s_current_state, 0, sizeof(s_current_state));
    s_num_fds = 0;

    for (int i = 0; i < MAX_INPUT_DEVS; i++) {
        char dev_path[32];
        snprintf(dev_path, sizeof(dev_path), "/dev/input/event%d", i);
        int fd = open(dev_path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            s_input_fds[s_num_fds++] = fd;
            printf("[+] Opened input device: %s\n", dev_path);
        }
    }

    return s_num_fds > 0 ? 0 : -1;
}

void mmp_input_close(void) {
    for (int i = 0; i < s_num_fds; i++) {
        if (s_input_fds[i] >= 0) {
            close(s_input_fds[i]);
            s_input_fds[i] = -1;
        }
    }
    s_num_fds = 0;
}

bool mmp_input_poll(MMPInputState *state) {
    bool menu_requested = false;
    struct input_event ev;

    for (int i = 0; i < s_num_fds; i++) {
        while (read(s_input_fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type != EV_KEY) continue;

            bool pressed = (ev.value != 0);

            switch (ev.code) {
                // Physical MENU / Home key
                case KEY_HOME:       // 102
                case KEY_RIGHTALT:   // 100
                case KEY_ESC:        // 1
                    s_current_state.menu = pressed;
                    if (pressed) menu_requested = true;
                    break;

                // D-Pad
                case KEY_UP:         // 103
                    s_current_state.up = pressed;
                    break;
                case KEY_DOWN:       // 108
                    s_current_state.down = pressed;
                    break;
                case KEY_LEFT:       // 105
                    s_current_state.left = pressed;
                    break;
                case KEY_RIGHT:      // 106
                    s_current_state.right = pressed;
                    break;

                // Action buttons (Miyoo Mini event0: A=KEY_SPACE, B=KEY_LEFTCTRL, Y=KEY_LEFTALT, X=KEY_LEFTSHIFT)
                case KEY_SPACE:      // 57 (A)
                    s_current_state.a = pressed;
                    break;
                case KEY_LEFTCTRL:   // 29 (B)
                    s_current_state.b = pressed;
                    break;
                case KEY_LEFTSHIFT:  // 42 (X)
                    s_current_state.x = pressed;
                    break;
                case KEY_LEFTALT:    // 56 (Y)
                case KEY_Z:          // 44
                case KEY_Y:
                    s_current_state.y = pressed;
                    break;

                // Shoulders
                case KEY_E:          // 18 (L1)
                    s_current_state.l1 = pressed;
                    break;
                case KEY_T:          // 20 (R1)
                    s_current_state.r1 = pressed;
                    break;
                case KEY_TAB:        // 15 (L2)
                    s_current_state.l2 = pressed;
                    break;
                case KEY_BACKSPACE:  // 14 (R2)
                    s_current_state.r2 = pressed;
                    break;

                // Start & Select
                case KEY_ENTER:      // 28 (Start)
                    s_current_state.start = pressed;
                    break;
                case KEY_RIGHTCTRL:  // (Select)
                    s_current_state.select = pressed;
                    break;

                default:
                    break;
            }

            // Fallback combo: START + SELECT = Exit
            if (s_current_state.start && s_current_state.select) {
                menu_requested = true;
            }
        }
    }

    if (state) {
        *state = s_current_state;
    }

    return menu_requested;
}
