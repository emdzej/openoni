#include "persist.h"

static int difficulty = 1;

/* ONrPersist_GetDifficulty 0x50f240 */
int persist_difficulty(void)
{
    return difficulty < 0 ? 0 : difficulty > 2 ? 2 : difficulty;
}

/* ONrPersist_SetDifficulty 0x50f260 */
void persist_set_difficulty(int d)
{
    difficulty = d < 0 ? 0 : d > 2 ? 2 : d;
}
