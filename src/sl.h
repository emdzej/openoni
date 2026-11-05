/* SL, the BSL script engine (SLrScript_*): the level's scripts (the .bsl files of GameDataFolder/IGMD/<level> and
   IGMD/global), their global variables, functions and threads. A script runs until it sleeps or a command
   blocks, and the scheduler picks it up again on a later tick (SLrScript_Update 0x478070).

   The lexer follows SLiToken_Make 0x47dac0 (its keywords, numbers with an "f" in front, "#" comments, line
   ends marking the token before). The functions are compiled to a small stack code with explicit frames so
   a thread can stop anywhere: the original walks its tokens with a C stack per script context; the code
   is the port's own, the behaviour is the language's. Format: docs/formats/bsl.md. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { SL_VOID, SL_INT, SL_FLOAT, SL_BOOL, SL_STRING } SlType;

typedef struct {
    SlType type;
    int32_t i;
    float f;
    const char *s;      /* SL_STRING: interned, lives as long as the database */
} SlValue;

/* A command (SLrScript_Command_Register_Void): argc values in, the result out. Returning false means
   "not done": the thread waits and the command is called again with the same arguments next tick (the
   *_block commands). */
typedef struct SlThread SlThread;
typedef bool (*SlCommand)(SlThread *t, int argc, const SlValue *argv, SlValue *result, void *user);

void sl_reset(void);
/* From a command: the thread sleeps that many ticks once the command is done (a blocking command
   that hands the script its wait, as playback_block does). */
void sl_thread_sleep(SlThread *t, uint32_t ticks);
/* Registers a command, replacing one of the same name. */
void sl_register(const char *name, SlCommand fn, void *user);
/* An engine variable (SLrGlobalVariable_Register_*): scripts read and set it by name. */
void sl_register_int(const char *name, int32_t *v);
void sl_register_float(const char *name, float *v);
void sl_register_bool(const char *name, bool *v);

/* Loads and compiles every .bsl of a folder (relative to the game data, through the file layer). False
   (err says why) on a parse error. Call for "GameDataFolder/IGMD/global" and the level's folder. */
bool sl_load_folder(const char *dir, char *err, size_t errcap);
/* The same for one source held in memory (name: for the errors), for tests and the console. */
bool sl_load_text(const char *name, const char *text, char *err, size_t errcap);
/* Starts a thread running a function, with string arguments (the hooks pass the character's name);
   false if there is no such function. */
bool sl_call(const char *func, int argc, const char *const *args);
/* The same, giving the thread's handle (0: none started), for sl_running: whether that thread still
   runs (the patrols' script points that wait for theirs). */
uint32_t sl_start(const char *func, int argc, const char *const *args);
bool sl_running(uint32_t id);
bool sl_function_exists(const char *func);
/* One tick (1/60 s): every thread runs until it sleeps, blocks or ends. */
void sl_update(void);
int sl_threads(void);

/* The database's numbers, for tests: files, functions, globals, code words. */
typedef struct { uint32_t files, functions, globals, code, tokens; uint32_t hash; } SlStats;
SlStats sl_stats(void);
/* Each command name the scripts call that nothing registered, once, through plat-independent logging. */
typedef void (*SlLogFn)(const char *msg);
void sl_set_log(SlLogFn fn);
void sl_set_trace(bool on);
/* SL_STRING values the commands make. */
const char *sl_intern(const char *s);
