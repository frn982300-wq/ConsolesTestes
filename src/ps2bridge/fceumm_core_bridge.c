/*
 * FCEUmm-PS2 bridge for the FCEUmm Interface Edition frontend.
 *
 * The original FCEUmm-PS2 core remains the only emulation core.
 * This bridge connects the frontend's USB-selected ROM path, DualShock
 * input, GSKit video, audsrv audio, save states and Game Genie/PAR cheats.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <malloc.h>

#include <gsKit.h>
#include <audsrv.h>

#include "core/driver.h"
#include "core/fceu.h"
#include "core/general.h"

#include "fceumm_core_bridge.h"

#define FCEUMM_VIDEO_WIDTH  256
#define FCEUMM_VIDEO_HEIGHT 240
#define FCEUMM_AUDIO_RATE   22050
#define FCEUMM_AUDIO_MAX_SAMPLES 2048

static GSGLOBAL *core_gs;
static GSTEXTURE nes_texture;
static u32 *nes_clut;
static int core_initialized;
static int core_game_loaded;
static int core_texture_ready;
static u32 core_input_word;
static unsigned int core_cheat_count;
static int16_t core_audio_buffer[FCEUMM_AUDIO_MAX_SAMPLES];

static char core_base_dir[128];
static char core_state_dir[160];
static char core_nv_dir[160];
/* External frontend CHT files are parsed by the frontend and applied through
   FCEUI_AddCheat. Disable the legacy native .cht auto-loader for this port. */
static char core_native_cheat_dir[160];

static int core_setup_video(void)
{
    memset(&nes_texture, 0, sizeof(nes_texture));

    nes_texture.PSM = GS_PSM_T8;
    nes_texture.ClutPSM = GS_PSM_CT32;
    nes_texture.Width = FCEUMM_VIDEO_WIDTH;
    nes_texture.Height = FCEUMM_VIDEO_HEIGHT;
    nes_texture.TBW = 4;
    nes_texture.Filter = GS_FILTER_NEAREST;

    nes_clut = (u32 *)memalign(128, gsKit_texture_size_ee(16, 16, GS_PSM_CT32));
    if (!nes_clut)
        return -1;

    memset(nes_clut, 0, gsKit_texture_size_ee(16, 16, GS_PSM_CT32));
    nes_texture.Clut = nes_clut;
    nes_texture.VramClut = gsKit_vram_alloc(
        core_gs,
        gsKit_texture_size(16, 16, GS_PSM_CT32),
        GSKIT_ALLOC_USERBUFFER
    );
    if (!nes_texture.VramClut)
        return -2;

    nes_texture.Vram = gsKit_vram_alloc(
        core_gs,
        gsKit_texture_size(FCEUMM_VIDEO_WIDTH, FCEUMM_VIDEO_HEIGHT, GS_PSM_T8),
        GSKIT_ALLOC_USERBUFFER
    );
    if (!nes_texture.Vram)
        return -3;

    core_texture_ready = 1;
    return 0;
}

static void core_update_palette(void)
{
    int i;
    uint8 r, g, b;

    if (!nes_clut)
        return;

    for (i = 0; i < 256; ++i) {
        FCEUD_GetPalette((uint8)i, &r, &g, &b);
        nes_clut[i] = ((u32)b << 16) |
                      ((u32)g << 8) |
                      (u32)r;
    }

    /* Match the CLUT ordering used by the tested FCEUmm-PS2 renderer. */
    for (i = 0; i < 256; ++i) {
        int modi = i & 63;
        if ((modi >= 8 && modi < 16) || (modi >= 40 && modi < 48)) {
            u32 tmp = nes_clut[i];
            nes_clut[i] = nes_clut[i + 8];
            nes_clut[i + 8] = tmp;
        }
    }
}

static int core_set_game_paths(const char *rom_path)
{
    const char *root = (rom_path && !strncmp(rom_path, "mass0:/", 7))
                     ? "mass0:" : "mass:";

    if (snprintf(core_base_dir, sizeof(core_base_dir), "%s/FCEUmm", root) < 0)
        return -1;
    if (snprintf(core_state_dir, sizeof(core_state_dir), "%s/FCEUmm/STATES", root) < 0)
        return -2;
    if (snprintf(core_nv_dir, sizeof(core_nv_dir), "%s/FCEUmm/STATES", root) < 0)
        return -3;
    if (snprintf(core_native_cheat_dir, sizeof(core_native_cheat_dir),
                 "%s/FCEUmm/.native-cheats-disabled", root) < 0)
        return -4;

    FCEUI_SetBaseDirectory(core_base_dir);
    FCEUI_SetDirOverride(FCEUIOD_STATE, core_state_dir);
    FCEUI_SetDirOverride(FCEUIOD_NV, core_nv_dir);
    FCEUI_SetDirOverride(FCEUIOD_CHEATS, core_native_cheat_dir);
    return 0;
}

int fceumm_core_init(GSGLOBAL *gs)
{
    if (core_initialized)
        return 0;
    if (!gs)
        return -1;

    core_gs = gs;

    if (!FCEUI_Initialize())
        return -2;

    FCEUI_SetVidSystem(0);              /* NTSC default */
    FCEUI_SetGameGenie(1);
    FCEUI_DisableSpriteLimitation(1);
    FCEUI_SetSoundVolume(1024);
    FCEUI_SetSoundQuality(0);
    FCEUI_SetLowPass(0);
    FCEUI_Sound(FCEUMM_AUDIO_RATE);

    core_input_word = 0;
    core_cheat_count = 0;
    core_initialized = 1;
    return 0;
}

int fceumm_core_set_audio_rate(unsigned rate)
{
    if (!core_initialized)
        return -1;
    if (rate != 11025u && rate != 22050u && rate != 44100u)
        return -2;
    FCEUI_Sound((int)rate);
    return 0;
}

int fceumm_core_load_game(const char *path)
{
    FCEUGI *loaded;
    int result;

    if (!core_initialized || !path || !path[0])
        return -1;

    if (core_game_loaded)
        fceumm_core_close_game();

    result = core_set_game_paths(path);
    if (result < 0)
        return -10 + result;

    loaded = FCEUI_LoadGame(path);
    if (!loaded)
        return -20;

    /* The PS2 port's pads.c consumes a packed 32-bit word:
       player 1 in bits 0..7, player 2 in bits 8..15. */
    FCEUI_SetInput(0, SI_GAMEPAD, &core_input_word, 0);
    FCEUI_SetInput(1, SI_GAMEPAD, &core_input_word, 0);
    FCEUI_DisableFourScore(1);

    if (!core_texture_ready) {
        result = core_setup_video();
        if (result < 0) {
            FCEUI_CloseGame();
            return -30 + result;
        }
    }

    core_update_palette();
    core_game_loaded = 1;

    core_gs->DrawOrder = GS_OS_PER;
    gsKit_mode_switch(core_gs, GS_PERSISTENT);
    gsKit_queue_reset(core_gs->Per_Queue);

    return 0;
}

void fceumm_core_close_game(void)
{
    if (core_game_loaded) {
        FCEUI_CloseGame();
        core_game_loaded = 0;
    }
    core_input_word = 0;
    core_cheat_count = 0;

    if (core_gs)
        gsKit_mode_switch(core_gs, GS_ONESHOT);
}

int fceumm_core_is_loaded(void)
{
    return core_game_loaded;
}

int fceumm_core_run_frame(u32 nes_buttons)
    uint8_t *frame = NULL;
    int32 *sound = NULL;
    int32 sample_count = 0;
    int i;
    int samples_to_play;

    if (!core_game_loaded || !core_gs || !core_texture_ready)
        return -1;

    core_input_word = (u32)nes_buttons;
    FCEUI_Emulate(&frame, &sound, &sample_count, 0);

    if (!frame)
        return -2;

    nes_texture.Mem = (u32 *)frame;
    gsKit_texture_upload(core_gs, &nes_texture);

    gsKit_clear(core_gs, GS_SETREG_RGBA(0, 0, 0, 0));
    gsKit_prim_sprite_texture(
        core_gs, &nes_texture,
        0.0f, 0.0f, 0.0f, 8.0f,
        640.0f, 448.0f, 256.0f, 232.0f,
        2, GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x00)
    );

    if (sound && sample_count > 0) {
        samples_to_play = sample_count;
        if (samples_to_play > FCEUMM_AUDIO_MAX_SAMPLES)
            samples_to_play = FCEUMM_AUDIO_MAX_SAMPLES;

        for (i = 0; i < samples_to_play; ++i) {
            int32 sample = sound[i];
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            core_audio_buffer[i] = (int16_t)sample;
        }
        (void)audsrv_play_audio((char *)core_audio_buffer,
                                samples_to_play * (int)sizeof(int16_t));
    }

    return 0;
}

static char *core_state_filename(int slot)
{
    if (!core_game_loaded || slot < 0 || slot >= 10)
        return NULL;
    return FCEU_MakeFName(FCEUMKF_STATE, slot, NULL);
}

int fceumm_core_save_state(int slot)
{
    char *path;
    FILE *check;

    if (!core_game_loaded || slot < 0 || slot >= 5)
        return -1;

    FCEUI_SelectState(slot);
    FCEUI_SaveState(NULL);

    path = core_state_filename(slot);
    if (!path)
        return -2;
    check = fopen(path, "rb");
    free(path);
    if (!check)
        return -3;
    fclose(check);
    return 0;
}

int fceumm_core_load_state(int slot)
{
    char *path;
    FILE *check;

    if (!core_game_loaded || slot < 0 || slot >= 5)
        return -1;

    path = core_state_filename(slot);
    if (!path)
        return -2;
    check = fopen(path, "rb");
    free(path);
    if (!check)
        return -3;
    fclose(check);

    FCEUI_SelectState(slot);
    FCEUI_LoadState(NULL);
    return 0;
}

void fceumm_core_clear_cheats(void)
{
    while (core_cheat_count > 0) {
        if (!FCEUI_DelCheat(core_cheat_count - 1))
            break;
        --core_cheat_count;
    }
}

static int core_is_hex_string(const char *s)
{
    int n = 0;
    if (!s)
        return 0;
    while (*s) {
        if (!((*s >= '0' && *s <= '9') ||
              (*s >= 'a' && *s <= 'f') ||
              (*s >= 'A' && *s <= 'F')))
            return 0;
        ++s;
        ++n;
    }
    return n == 8;
}

static int core_is_game_genie_string(const char *s)
{
    int n = 0;
    const char *alphabet = "APZLGITYEOXUKSVN";
    if (!s)
        return 0;
    while (*s) {
        const char *p;
        int found = 0;
        for (p = alphabet; *p; ++p) {
            if (*p == (char)((*s >= 'a' && *s <= 'z') ? (*s - ('a' - 'A')) : *s)) {
                found = 1;
                break;
            }
        }
        if (!found)
            return 0;
        ++s;
        ++n;
    }
    return n == 6 || n == 8;
}

int fceumm_core_add_cheat(const char *name, const char *code)
{
    char normalized[32];
    size_t n = 0;
    uint16_t address = 0;
    uint8_t value = 0;
    int compare = -1;
    int type = 1;
    const char *p;

    if (!core_game_loaded || !name || !code)
        return 0;

    for (p = code; *p && n < sizeof(normalized) - 1; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '-' || *p == ':')
            continue;
        normalized[n++] = *p;
    }
    normalized[n] = '\0';

    if (core_is_game_genie_string(normalized)) {
        if (!FCEUI_DecodeGG(normalized, &address, &value, &compare))
            return 0;
        type = 1;
    } else if (core_is_hex_string(normalized)) {
        if (!FCEUI_DecodePAR(normalized, &address, &value, &compare, &type))
            return 0;
    } else {
        return 0;
    }

    if (!FCEUI_AddCheat(name, address, value, compare, type))
        return 0;

    ++core_cheat_count;
    return 1;
}

void fceumm_core_shutdown(void)
{
    fceumm_core_close_game();
    if (core_initialized) {
        FCEUI_Kill();
        core_initialized = 0;
    }
    if (nes_clut) {
        free(nes_clut);
        nes_clut = NULL;
    }
    core_texture_ready = 0;
    core_gs = NULL;
}
