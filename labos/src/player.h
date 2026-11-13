// Experiment films from the USB stick: labos/films/<name>-<height>.lfilm, decoded by h264bsd.
#ifndef LABOS_PLAYER_H
#define LABOS_PLAYER_H

#include "efi.h"

// Loads the largest film that fits the screen and starts it; 0 when no file was found or readable.
// On failure player_error() says why
int player_open(const char *name);
void player_close(void);
const char *player_error(void);

// Decodes and shows the frames that are due; call it often from the main loop
void player_tick(void);

void player_toggle_pause(void);
void player_seek(int seconds);
void player_restart(void);

// "frame 312 of 3135, 29.8 fps, 4 dropped" for the network
usize player_state(char *out, usize cap);

#endif
