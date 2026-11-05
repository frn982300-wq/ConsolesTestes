#ifndef FCEUMM_CORE_BRIDGE_H
#define FCEUMM_CORE_BRIDGE_H

#include <stdint.h>
#include <gsKit.h>

int fceumm_core_init(GSGLOBAL *gs);
int fceumm_core_load_game(const char *path);
int fceumm_core_set_audio_rate(unsigned rate);
void fceumm_core_close_game(void);
int fceumm_core_is_loaded(void);
int fceumm_core_run_frame(uint32_t nes_buttons);
int fceumm_core_save_state(int slot);
int fceumm_core_load_state(int slot);
void fceumm_core_clear_cheats(void);
int fceumm_core_add_cheat(const char *name, const char *code);
void fceumm_core_shutdown(void);

#endif
