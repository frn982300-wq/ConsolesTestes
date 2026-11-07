#include <stdio.h>
#include "../core/fceu-types.h"
#include "../core/driver.h"

struct pcpal {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} pcpalette[256];
//extern unsigned int ps2palette[256];

// Main
FILE *FCEUD_UTF8fopen(const char *fn, const char *mode)
{
    return(fopen(fn, mode));
}

// Functions to display messages differently
void FCEUD_PrintError(char *s)
{
    printf("Error %s", s);
}

void FCEUD_Message(char *s)
{
    printf("%s", s);
}

void FCEUD_SetEmulationSpeed(int cmd)
{
    printf("SetEmulationSpeed not implemented.\n");
}

// Sound related functions
// could be implemented, but no real reason to
static int soundvolume = 1024;
void FCEUD_SoundVolumeAdjust(int adjust)
{
    switch(adjust)
    {
    case -1: soundvolume -=  50; if (soundvolume <    0) soundvolume =    0; break; // Lower volume
    case  0: soundvolume = 1024; break;
    case  1: soundvolume +=  50; if (soundvolume > 1024) soundvolume = 1024; break; // Raise volume
    }
    FCEUI_SetSoundVolume(soundvolume);
    FCEU_DispMessage("Sound volume %d.", soundvolume);
}

int mute = 0;
void FCEUD_SoundToggle(void)
{
    if (mute) {
        mute = 0;
        FCEUI_SetSoundVolume(soundvolume);
        FCEU_DispMessage("Sound mute off.");
    }
    else {
        mute = 1;
        FCEUI_SetSoundVolume(0);
        FCEU_DispMessage("Sound mute on.");
    }
}

// Network
int FCEUD_SendData(void *data, uint32 len)
{
    printf("Send Network Data");
    return(0);
}

void FCEUD_NetworkClose(void)
{
    printf("Close Network");
}

int FCEUD_RecvData(void *data, uint32 len)
{
    printf("Receive Network Data");
    return(0);
}

void FCEUD_NetplayText(uint8 *text)
{
    printf("Network Text");
}

// Video
void FCEUD_SetPalette(uint8 index, uint8 r, uint8 g, uint8 b)
{
    pcpalette[index].r = r;
    pcpalette[index].g = g;
    pcpalette[index].b = b;
}

void FCEUD_GetPalette(uint8 i,uint8 *r, uint8 *g, uint8 *b)
{
    *r = pcpalette[i].r;
    *g = pcpalette[i].g;
    *b = pcpalette[i].b;
}


#define DUMMY(f) void f(void) { FCEU_DispMessage("Not implemented."); }
//#define DUMMY(f) void f(void) {;}; // Was getting the message above, but seems to have disappeared
DUMMY(FCEUD_HideMenuToggle)
DUMMY(FCEUD_TurboOn)
DUMMY(FCEUD_TurboOff)
DUMMY(FCEUD_SaveStateAs)
DUMMY(FCEUD_LoadStateFrom)
DUMMY(FCEUD_MovieRecordTo)
DUMMY(FCEUD_MovieReplayFrom)
DUMMY(FCEUD_ToggleStatusIcon)
DUMMY(FCEUD_AviRecordTo)
DUMMY(FCEUD_AviStop)
void FCEUI_AviVideoUpdate(const unsigned char* buffer) { /*FCEU_DispMessage("Not implemented.");*/ }
int FCEUD_ShowStatusIcon(void) { /*FCEU_DispMessage("Not implemented.");*/ return 0; }
int FCEUI_AviIsRecording(void) { return 0; }


/*
 * PS2 compatibility stubs for optional desktop-only facilities.
 *
 * The PPU viewer is a desktop debugging window.  The PS2 frontend renders
 * through GSKit, so keep the viewer disabled and provide no-op symbols for
 * the core's optional debug hook.
 */
#include "../core/ppuview.h"

int PPUViewScanline = 0;
int PPUViewer = 0;

void PPUViewDoBlit(void)
{
}

void DoPPUView(void)
{
}

void UpdatePPUView(int refreshchr)
{
    (void)refreshchr;
}

/*
 * The Transformer board can read a keyboard.  The PS2 frontend currently
 * has no keyboard device mapping; return a stable, zeroed 256-byte key map
 * instead of the desktop driver's missing symbol or an unsafe empty string.
 * Standard gamepad-based NES/Famicom games are unaffected.
 */
char *GetKeyboard(void)
{
    static char ps2_keyboard_state[256] = { 0 };
    return ps2_keyboard_state;
}
