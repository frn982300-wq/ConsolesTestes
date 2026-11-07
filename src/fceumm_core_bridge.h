#ifndef FCEUMM_CORE_BRIDGE_H
#define FCEUMM_CORE_BRIDGE_H

#include <stdint.h>
#include <gsKit.h>

int fceumm_core_init(GSGLOBAL *gs);
int fceumm_core_load_game(const char *path);
int fceumm_core_set_audio_rate(unsigned rate);
void fceumm_core_close_game(void);
int fceumm_core_is_loaded(void);
int fceumm_core_run_frame(unsigned int nes_buttons);
int fceumm_core_audio_flush(void);
int fceumm_core_save_state(int slot);
int fceumm_core_load_state(int slot);
void fceumm_core_clear_cheats(void);
int fceumm_core_add_cheat(const char *name, const char *code);
void fceumm_core_shutdown(void);
void fceumm_core_debug_event(const char *event, int value);
void fceumm_core_debug_snapshot(unsigned int *attempts, unsigned int *completed,
                                 int *stage, int *last_error, int *last_samples,
                                 int *last_audio_result);

#endif
