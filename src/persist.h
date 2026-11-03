/* The player's persistent settings (ONrPersist): for now the difficulty, 0 easy, 1 normal, 2 hard
   (0x5ed388, 1 at start-up). Saving them is not yet ported. */
#pragma once

/* ONrPersist_GetDifficulty 0x50f240: clamped to 0..2. */
int persist_difficulty(void);
/* ONrPersist_SetDifficulty 0x50f260: clamped to 0..2 (and saved, not yet ported). */
void persist_set_difficulty(int d);
