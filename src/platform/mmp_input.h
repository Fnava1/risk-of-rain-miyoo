#ifndef MMP_INPUT_H
#define MMP_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool up;
    bool down;
    bool left;
    bool right;
    bool a;
    bool b;
    bool x;
    bool y;
    bool l1;
    bool r1;
    bool l2;
    bool r2;
    bool start;
    bool select;
    bool menu; // Physical central MENU button
} MMPInputState;

/**
 * Initializes evdev input readers for Miyoo Mini (/dev/input/event*).
 */
int mmp_input_init(void);

/**
 * Polls hardware input events non-blockingly.
 * Returns true if the MENU button was pressed (requesting exit).
 */
bool mmp_input_poll(MMPInputState *state);

/**
 * Closes input descriptors.
 */
void mmp_input_close(void);

#ifdef __cplusplus
}
#endif

#endif // MMP_INPUT_H
