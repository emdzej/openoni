/* The application: what the platform backend runs. app_main is the port of ONiMain 0x4d3280 (step 1: the
   exe check and a test screen) and keeps the original's loops (platform.h). */
#pragma once
#include <stdbool.h>

enum { APP_FRAME_HZ = 60, APP_AUDIO_RATE = 44100 };

/* Runs the game; returns the exit code. Called once, in the first frame, with the data mounted. */
int app_main(void);
/* The player quit (closed the window): save what the game would save. app_main doesn't run again. */
void app_exit(void);
/* The frame's audio: frames x 2 floats (stereo), called from plat_wait_frame. */
void app_audio(float *stereo, unsigned frames);
