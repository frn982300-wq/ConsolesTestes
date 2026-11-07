          /*
           * FCEUmm PS2 - integrated NES/Famicom frontend.
           * Reuses the tested PCE Fast frontend shell; FCEUmm-PS2 0.3.3 is the only core.
           * The FCEUmm-PS2 0.3.3 core is linked as the only emulation core.
           * No ROM or firmware bundled. Uses the previously tested V10/V11 exFAT driver pair.
           * audsrv is initialized for the FCEUmm mono audio stream at 22050 Hz.
           */
          /* POSIX types are supplied through the explicit PS2 toolchain newlib header in EE_CFLAGS. */
          #include <gsKit.h>
          #include <dmaKit.h>
          #include <libpad.h>
          #include <sifrpc.h>
          #include <loadfile.h>
          #include <sbv_patches.h>
          #include <iopheap.h>
          #include <fileXio_rpc.h>
          #include <io_common.h>
          #include <iox_stat.h>
                    #include <audsrv.h>
          #include <stdio.h>
          #include <stdlib.h>
          #include <string.h>
          /* Local ASCII-only ctype helpers avoid unresolved PS2SDK newlib symbols. */
static int fceu_ascii_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c; }
static int fceu_ascii_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c; }
static int fceu_ascii_isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
static int fceu_ascii_isalnum(int c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
#define tolower(c) fceu_ascii_tolower((unsigned char)(c))
#define toupper(c) fceu_ascii_toupper((unsigned char)(c))
#define isspace(c) fceu_ascii_isspace((unsigned char)(c))
#define isalnum(c) fceu_ascii_isalnum((unsigned char)(c))

          #include <stdint.h>
          #include <zlib.h>
#include "fceumm_embedded_background.h"
#include "fceumm_core_bridge.h"
          /* Some PS2SDK/newlib headers do not expose snprintf under the current
             feature macros. Declare its standard signature explicitly. */
          extern int snprintf(char *str, size_t size, const char *format, ...);

          #define MAX_FILES 128
          #define MAX_PATH 512
                              /* Diagnostic overlays are disabled in this release test. */
          #define FCEU_SHOW_DIAGNOSTICS 0

          extern unsigned char usbd_irx[], usbhdfsd_irx[], iomanx_irx[], filexio_irx[], audsrv_irx[];
          extern unsigned int size_usbd_irx, size_usbhdfsd_irx, size_iomanx_irx, size_filexio_irx, size_audsrv_irx;

          typedef struct { char name[256]; int dir; } Entry;
          static Entry entries[MAX_FILES];
          static int nentries, selected, screen, status_code;
          static char folder[MAX_PATH] = "mass:/";
          static char game_path[MAX_PATH];
          static char game_member_path[MAX_PATH]; /* NES member name inside a ZIP, or direct ROM path. */
          /* ROM file-size details shown if the core rejects a selected game. */
          static unsigned long last_rom_size;
                                                  static char pad_buf[256] __attribute__((aligned(64)));
          static struct padButtonStatus pad_info;
          static unsigned int buttons_now, buttons_prev, buttons_edge;
          /* Hold-to-fire turbo: about 15 pulses/second at the FCEUmm ~60 Hz frame rate. */
          #define FCEU_TURBO_PERIOD_FRAMES 4u
          #define FCEU_TURBO_ON_FRAMES 2u
          static unsigned int turbo_i_counter, turbo_ii_counter;
          static int turbo_i_active, turbo_ii_active;
          /* The interface test loop advances Turbo once per screen refresh. */
          static int turbo_updated_this_frame;
          static iox_dirent_t dirent_buf __attribute__((aligned(64)));
          static iox_stat_t stat_buf __attribute__((aligned(64)));
          static unsigned char readbuf[16384] __attribute__((aligned(64)));
                    /* 200x200 PNG covers are uploaded into a separate 256x256 PSMCT16 texture. */
#define COVER_TEX_SIZE 256
#define COVER_PNG_MAX_BYTES (2u * 1024u * 1024u)
static uint16_t cover_pixels[COVER_TEX_SIZE * COVER_TEX_SIZE] __attribute__((aligned(64)));
static GSTEXTURE cover_tex;
/* Embedded menu background: source image is compiled into this ELF. */
#define PCEFAST_BG_TEX_WIDTH 1024
#define PCEFAST_BG_TEX_HEIGHT 512
static uint16_t background_pixels[PCEFAST_BG_TEX_WIDTH * PCEFAST_BG_TEX_HEIGHT] __attribute__((aligned(64)));
static unsigned char background_raw[PCEFAST_BG_RAW_SIZE] __attribute__((aligned(64)));
static GSTEXTURE background_tex;
static int background_ready;
static iox_stat_t cover_stat_buf __attribute__((aligned(64)));
static int cover_tex_ready, cover_loaded, cover_error;
static char cover_key[MAX_PATH];
static char cover_debug[96] = "NO COVER CHECK";
          static GSGLOBAL *gs;
                              static int interface_session_active;
          static int usb_initialized, iop_prepared;
          static int pad_ready;
          /* Port-open status is not proof of live input. */
          static int pad_read_ok, pad_state_value = -1, pad_read_len;
          static unsigned int pad_raw_buttons = 0xffffu;
          static int pad_init_result, pad_open_result;
          #if FCEU_SHOW_DIAGNOSTICS
          static unsigned int input_active_mask, input_query_mask;
          static unsigned int input_mask_query_count;
          static unsigned int input_poll_count, input_state_count;
          static int input_last_port = -1, input_last_device = -1;
          static int input_last_id = -1, input_last_value;
          static unsigned int input_a_queries, input_b_queries;
          static int input_a_value, input_b_value;
          #endif
          static int audio_module_loaded, audio_rpc_ready, audio_ready, audio_error;
          static unsigned audio_rate = 22050;

          #define FCEU_STATE_SLOTS 5
static int state_menu_selected = 0;
          static char state_message[64] = "";
          static int state_message_frames;

          /* External NES .cht files are parsed by the frontend and applied through FCEUmm. */
          #define FCEU_CHEAT_MAX 64
          #define FCEU_CHEAT_DESC 72
          #define FCEU_CHEAT_CODE 160
          typedef struct {
            unsigned int source_index;
            char desc[FCEU_CHEAT_DESC];
            char code[FCEU_CHEAT_CODE];
            int enabled;
          } FCEUCheat;
          static FCEUCheat fceu_cheats[FCEU_CHEAT_MAX];
          static unsigned int active_cheat_count = 0;
          #define FCEU_CHEAT_MENU_ITEMS ((int)active_cheat_count + 2)
          static int cheat_menu_selected = 0;
          static char active_cht_path[MAX_PATH] = "";
          static void state_set_message(const char *message);
static void apply_enabled_cheats(void);

          static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-/:._()[]";
static const unsigned long long glyphs[] = {
            0x004631fc62eULL,0x003e317c62fULL,0x0078210843eULL,0x003e318c62fULL,0x007c217843fULL,
            0x0004217843fULL,0x003a31e843eULL,0x004631fc631ULL,0x007c842109fULL,0x0019294211cULL,
            0x00452519531ULL,0x007c2108421ULL,0x004631ad771ULL,0x004631cd671ULL,0x003a318c62eULL,
            0x0004217c62fULL,0x0059358c62eULL,0x0045257c62fULL,0x003e107043eULL,0x0010842109fULL,
            0x003a318c631ULL,0x0011518c631ULL,0x004775ac631ULL,0x00462a22a31ULL,0x00108422a31ULL,
            0x007c222221fULL,0x007a3e83800ULL,0x003e319b421ULL,0x003a210f800ULL,0x007a31cda10ULL,
            0x00383f8b800ULL,0x00084238a4cULL,0x003a1e8c7c0ULL,0x0046319b421ULL,0x00388421804ULL,
            0x00192843008ULL,0x0024a32a421ULL,0x00388421086ULL,0x0056b5aac00ULL,0x0046319b400ULL,
            0x003a318b800ULL,0x00042f8c5e0ULL,0x00421e8c7c0ULL,0x0004219b400ULL,0x003e0e0f800ULL,
            0x00324211c42ULL,0x005b318c400ULL,0x0011518c400ULL,0x002ab58c400ULL,0x00454454400ULL,
            0x003a1e8c620ULL,0x007c4447c00ULL,0x003a33ae62eULL,0x003884210c4ULL,0x007c444422eULL,
            0x003e107420fULL,0x00211f4a988ULL,0x003e107843fULL,0x003a317842eULL,0x0008422221fULL,
            0x003a317462eULL,0x003a10f462eULL,0x000000f8000ULL,0x00042222210ULL,0x00008401080ULL,
            0x0018c000000ULL,0x00000000000ULL,0x0030821088cULL,0x00188842086ULL,0x0038421084eULL,
            0x0039084210eULL
          };
          static void label(int x,int y,int scale,const char *s,uint64_t color) {
            int i,r,c,j;
            for(i=0;s[i];i++) {
              char ch=s[i];
              unsigned long long bits=0;
              for(j=0;chars[j];j++) if(chars[j]==ch) {bits=glyphs[j];break;}
              for(r=0;r<7;r++) for(c=0;c<5;c++) if(bits&(1ULL << (r*5+c))) {
                float xx=(float)(x+(i*6+c)*scale), yy=(float)(y+r*scale);
                gsKit_prim_sprite(gs,xx,yy,xx+scale,yy+scale,1,color);
              }
            }
          }
          static int init_embedded_background(void) {
            uLongf raw_size=(uLongf)PCEFAST_BG_RAW_SIZE;
            unsigned int y;
            int zr;
            memset(&background_tex,0,sizeof(background_tex));
            memset(background_pixels,0,sizeof(background_pixels));
            zr=uncompress((Bytef *)background_raw,&raw_size,
                          (const Bytef *)pcefast_background_zlib,
                          (uLong)PCEFAST_BG_ZLIB_SIZE);
            if(zr!=Z_OK || raw_size!=(uLongf)PCEFAST_BG_RAW_SIZE) return 0;
            for(y=0;y<PCEFAST_BG_HEIGHT;y++) {
              memcpy(&background_pixels[(size_t)y*PCEFAST_BG_TEX_WIDTH],
                     &background_raw[(size_t)y*PCEFAST_BG_WIDTH*2u],
                     (size_t)PCEFAST_BG_WIDTH*2u);
            }
            background_tex.Width=PCEFAST_BG_TEX_WIDTH;
            background_tex.Height=PCEFAST_BG_TEX_HEIGHT;
            background_tex.PSM=GS_PSM_CT16;
            background_tex.Mem=(u32 *)background_pixels;
            background_tex.Vram=gsKit_vram_alloc(gs,
              gsKit_texture_size(PCEFAST_BG_TEX_WIDTH,PCEFAST_BG_TEX_HEIGHT,GS_PSM_CT16),
              GSKIT_ALLOC_USERBUFFER);
            if(!background_tex.Vram) return 0;
            gsKit_texture_upload(gs,&background_tex);
            background_ready=1;
            return 1;
          }
          static void draw_embedded_background(void) {
            if(!background_ready) return;
            gsKit_prim_sprite_texture(gs,&background_tex,
              0.0f,0.0f,0.0f,0.0f,640.0f,448.0f,
              (float)PCEFAST_BG_WIDTH,(float)PCEFAST_BG_HEIGHT,1,
              GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0));
          }
          static void title_screen(const char *subtitle) {
            uint64_t bg=GS_SETREG_RGBAQ(12,20,35,0x80,0);
            uint64_t white=GS_SETREG_RGBAQ(230,240,245,0x80,0);
            uint64_t aqua=GS_SETREG_RGBAQ(50,210,175,0x80,0);
            gsKit_clear(gs,bg);
            draw_embedded_background();
            label(45,48,3,"FCEUMM INTERFACE",white);
            label(45,99,2,subtitle,aqua);
          }
          static void refresh_screen(void) { gsKit_queue_exec(gs); gsKit_sync_flip(gs); }
          static void pad_scan(void) {
            int s, n;
            buttons_prev=buttons_now;
            pad_read_ok=0;
            pad_read_len=0;
            if(!pad_ready) {
              pad_state_value=-1; pad_raw_buttons=0xffffu;
              buttons_now=0; buttons_edge=0; return;
            }
            s=padGetState(0,0);
            pad_state_value=s;
            if(s!=PAD_STATE_STABLE && s!=PAD_STATE_FINDCTP1) {
              pad_raw_buttons=0xffffu;
              buttons_now=0; buttons_edge=0; return;
            }
            n=(int)padRead(0,0,&pad_info);
            pad_read_len=n;
            if(n<=0) {
              pad_raw_buttons=0xffffu;
              buttons_now=0; buttons_edge=0; return;
            }
            pad_read_ok=1;
            pad_raw_buttons=(unsigned int)pad_info.btns & 0xffffu;
            buttons_now=(~pad_raw_buttons) & 0xffffu;
            buttons_edge=buttons_now & ~buttons_prev;
          }
          /* Browser-only hold-repeat: immediate first move, then 15 frames delay,
             repeat every 4 frames, accelerating to every 2 frames after 45 frames. */
          static int nav_repeat(unsigned int button) {
            static unsigned int up_frames=0, down_frames=0;
            unsigned int *frames = (button==PAD_UP) ? &up_frames : &down_frames;
            if(!(buttons_now & button)) { *frames=0; return 0; }
            if(buttons_edge & button) { *frames=0; return 1; }
            ++(*frames);
            if(*frames < 15u) return 0;
            if(*frames < 45u) return (((*frames-15u)%4u)==0u);
            return (((*frames-45u)%2u)==0u);
          }
          static int load_mod(void *address,unsigned int len) {
            int result=0, id=SifExecModuleBuffer(address,len,0,NULL,&result);
            return (id<0)?id:((result<0)?result:0);
          }
          static void usb_progress(const char *stage) {
            title_screen("FCEUMM USB TEST");
            label(52,210,2,stage,GS_SETREG_RGBAQ(50,210,175,0x80,0));
            refresh_screen();
          }
          static int usb_init(void) {
            int r;
            if(usb_initialized)return 0;
            usb_progress("INIT SIF / IOP");
            SifLoadFileInit(); SifInitIopHeap(); sbv_patch_enable_lmb(); iop_prepared=1;
            usb_progress("LOAD IOMANX");
            r=load_mod(iomanx_irx,size_iomanx_irx); if(r<0)return r;
            usb_progress("LOAD FILEXIO");
            r=load_mod(filexio_irx,size_filexio_irx); if(r<0)return r;
            usb_progress("INIT FILEXIO");
            r=fileXioInit(); if(r<0)return r;
            usb_progress("LOAD EXFAT USBD");
            r=load_mod(usbd_irx,size_usbd_irx); if(r<0)return r;
            usb_progress("LOAD EXFAT HDFSD");
            r=load_mod(usbhdfsd_irx,size_usbhdfsd_irx); if(r<0)return r;
            usb_initialized=1;
            usb_progress("EXFAT DRIVERS READY");
            return 0;
          }
          static int audio_init(unsigned requested_rate) {
            int r;
            int freq;
            if(audio_ready) return 0;
            if(!iop_prepared) {
              SifLoadFileInit(); SifInitIopHeap(); sbv_patch_enable_lmb(); iop_prepared=1;
            }
            if(!audio_module_loaded) {
              /* LIBSD is the PS2 IOP sound driver required by audsrv. */
              (void)SifLoadModule("rom0:LIBSD",0,NULL);
              r=load_mod(audsrv_irx,size_audsrv_irx);
              if(r<0) { audio_error=r; return r; }
              audio_module_loaded=1;
            }
            if(!audio_rpc_ready) {
              r=audsrv_init();
              if(r<0) { audio_error=r; return r; }
              audio_rpc_ready=1;
            }
            freq=(int)(requested_rate ? requested_rate : 22050);
            r=audsrv_set_format(freq,16,1);
            if(r<0 && freq!=44100) { freq=44100; r=audsrv_set_format(freq,16,1); }
            if(r<0 && freq!=11025) { freq=11025; r=audsrv_set_format(freq,16,1); }
            if(r<0) { audio_error=r; return r; }
            r=audsrv_set_volume(90);
            if(r<0) { audio_error=r; return r; }
            audio_rate=(unsigned)freq;
            audio_ready=1; audio_error=0;
            return 0;
          }
          /* FCEUmm supplies indexed 256x240 frames to the GSKit bridge. */
          static int has_extension_ci(const char *name, const char *extension) {
            const char *dot;
            size_t n;
            if(!name || !extension) return 0;
            dot=strrchr(name,'.');
            if(!dot) return 0;
            n=strlen(extension);
            if(strlen(dot)!=n) return 0;
            while(*extension) {
              if(tolower((unsigned char)*dot++) != tolower((unsigned char)*extension++))
                return 0;
            }
            return 1;
          }
          static int is_nes(const char *name) { return has_extension_ci(name,".nes"); }
          static int is_zip(const char *name) { return has_extension_ci(name,".zip"); }
          static int is_unf(const char *name) { return has_extension_ci(name,".unf"); }
          static int is_unif(const char *name) { return has_extension_ci(name,".unif"); }
          static int is_nsf(const char *name) { return has_extension_ci(name,".nsf"); }
          static int is_nez(const char *name) { return has_extension_ci(name,".nez"); }
          static int is_supported_rom(const char *name) {
            return is_nes(name) || is_zip(name) || is_unf(name) ||
                   is_unif(name) || is_nsf(name) || is_nez(name);
          }
          /* No temporary ROMs are created in Interface Edition. */
          static int cmp_entry(const void *pa,const void *pb) {
            const Entry *a=(const Entry *)pa,*b=(const Entry *)pb;
            if(a->dir != b->dir) return b->dir-a->dir;
            {
              int i;
              for(i=0;i<256;i++) {
                int ca=tolower((unsigned char)a->name[i]);
                int cb=tolower((unsigned char)b->name[i]);
                if(ca!=cb)return ca-cb;
                if(!ca)return 0;
              }
              return 0;
            }
          }
          static int scan_dir(void) {
            int d=fileXioDopen(folder),r;
            nentries=0; selected=0;
            if(d<0) return d;
            for(;;) {
              memset(&dirent_buf,0,sizeof(dirent_buf));
              r=fileXioDread(d,&dirent_buf);
              if(r<=0)break;
              dirent_buf.name[sizeof(dirent_buf.name)-1]=0;
              if(!strcmp(dirent_buf.name,".")||!strcmp(dirent_buf.name,".."))continue;
              /* Only list .nes and .zip files in FCEUmm/nes roms/. */
              if(FIO_S_ISDIR(dirent_buf.stat.mode)) continue;
              if(!is_supported_rom(dirent_buf.name)) continue;
              if(nentries>=MAX_FILES)continue;
              strncpy(entries[nentries].name,dirent_buf.name,sizeof(entries[0].name)-1);
              entries[nentries].name[sizeof(entries[0].name)-1]=0;
              entries[nentries].dir=FIO_S_ISDIR(dirent_buf.stat.mode)?1:0;
              ++nentries;
            }
            fileXioDclose(d);
            qsort(entries,nentries,sizeof(entries[0]),cmp_entry);
            return r<0?r:0;
          }
          static void enter_parent(void) {
            const char *base = !strncmp(folder,"mass0:/",7)
              ? "mass0:/FCEUmm/nes roms/" : "mass:/FCEUmm/nes roms/";
            size_t n=strlen(folder), root=strlen(base);
            /* The ROM browser is intentionally confined to nes roms/. */
            if(n<=root) return;
            if(folder[n-1]=='/') --n;
            while(n>root && folder[n-1]!='/') --n;
            if(n<root) return;
            folder[n]=0;
            status_code=scan_dir();
          }

/* --- 200x200 PNG game-cover support (RGB, RGBA, or indexed PNG; 8-bit, non-interlaced). --- */
static uint32_t png_be32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static int png_paeth(int a, int b, int c) {
  int p=a+b-c, pa=abs(p-a), pb=abs(p-b), pc=abs(p-c);
  if(pa<=pb && pa<=pc) return a;
  if(pb<=pc) return b;
  return c;
}
static int read_cover_file(const char *path, unsigned char **out, size_t *out_size) {
  int fd,n;
  size_t size,off=0;
  unsigned char *data;
  if(!path || !out || !out_size) return -1;
  *out=NULL; *out_size=0;
  memset(&cover_stat_buf,0,sizeof(cover_stat_buf));
  if(fileXioGetStat(path,&cover_stat_buf)<0) return -1;
  if(cover_stat_buf.hisize || !cover_stat_buf.size ||
     cover_stat_buf.size>COVER_PNG_MAX_BYTES) return -2;
  size=(size_t)cover_stat_buf.size;
  data=(unsigned char *)malloc(size);
  if(!data) return -3;
  fd=fileXioOpen(path,FIO_O_RDONLY);
  if(fd<0) { free(data); return -4; }
  if(fileXioLseek(fd,0,FIO_SEEK_SET)!=0) {
    fileXioClose(fd); free(data); return -5;
  }
  while(off<size) {
    int want=(int)(size-off);
    if(want>(int)sizeof(readbuf)) want=(int)sizeof(readbuf);
    n=fileXioRead(fd,readbuf,want);
    if(n<=0 || n>want) {
      fileXioClose(fd); free(data); return -6;
    }
    memcpy(data+off,readbuf,(size_t)n);
    off+=(size_t)n;
  }
  if(fileXioClose(fd)<0) { free(data); return -7; }
  *out=data; *out_size=size;
  return 0;
}
static int decode_cover_png_200(const unsigned char *png, size_t png_size) {
  static const unsigned char signature[8]={137,80,78,71,13,10,26,10};
  unsigned char palette[768], palette_alpha[256];
  unsigned int palette_entries=0, palette_alpha_count=0;
  unsigned int width=0,height=0,bit_depth=0,color_type=0;
  unsigned int bytes_per_pixel=0;
  int got_ihdr=0,got_iend=0,ok=0;
  size_t pos=8,idat_size=0,row_bytes,raw_size,decoded_size;
  unsigned char *idat=NULL,*raw=NULL,*decoded=NULL;
  if(!png || png_size<8 || memcmp(png,signature,8)!=0) return 0;
  memset(palette,0,sizeof(palette));
  memset(palette_alpha,255,sizeof(palette_alpha));
  idat=(unsigned char *)malloc(png_size);
  if(!idat) return 0;
  while(pos<=png_size && png_size-pos>=12) {
    uint32_t length=png_be32(png+pos);
    const unsigned char *type=png+pos+4;
    const unsigned char *data=png+pos+8;
    uint32_t expected_crc,actual_crc;
    size_t chunk_total;
    if((size_t)length>png_size-pos-12u) goto done;
    chunk_total=12u+(size_t)length;
    expected_crc=png_be32(data+length);
    actual_crc=(uint32_t)crc32(0L,Z_NULL,0);
    actual_crc=(uint32_t)crc32(actual_crc,(const Bytef *)type,(uInt)(4u+length));
    if(actual_crc!=expected_crc) goto done;
    if(memcmp(type,"IHDR",4)==0) {
      if(got_ihdr || length!=13u || pos!=8u) goto done;
      width=png_be32(data); height=png_be32(data+4);
      bit_depth=data[8]; color_type=data[9];
      if(data[10]!=0 || data[11]!=0 || data[12]!=0) goto done;
      if(width!=200u || height!=200u || bit_depth!=8u) goto done;
      if(color_type!=0u && color_type!=2u && color_type!=3u &&
         color_type!=4u && color_type!=6u) goto done;
      if(color_type==2u) bytes_per_pixel=3u;
      else if(color_type==4u) bytes_per_pixel=2u;
      else if(color_type==6u) bytes_per_pixel=4u;
      else bytes_per_pixel=1u;
      got_ihdr=1;
    } else if(memcmp(type,"PLTE",4)==0) {
      if(!length || length>sizeof(palette) || (length%3u)!=0) goto done;
      memcpy(palette,data,length);
      palette_entries=(unsigned int)(length/3u);
    } else if(memcmp(type,"tRNS",4)==0) {
      if(length>sizeof(palette_alpha)) length=sizeof(palette_alpha);
      memcpy(palette_alpha,data,length);
      palette_alpha_count=(unsigned int)length;
    } else if(memcmp(type,"IDAT",4)==0) {
      if(!got_ihdr || (size_t)length>png_size-idat_size) goto done;
      memcpy(idat+idat_size,data,(size_t)length);
      idat_size+=(size_t)length;
    } else if(memcmp(type,"IEND",4)==0) {
      if(length!=0u) goto done;
      got_iend=1;
      pos+=chunk_total;
      break;
    }
    pos+=chunk_total;
  }
  if(!got_ihdr || !got_iend || !idat_size ||
     (color_type==3u && !palette_entries)) goto done;
  row_bytes=(size_t)width*bytes_per_pixel;
  raw_size=(row_bytes+1u)*(size_t)height;
  decoded_size=row_bytes*(size_t)height;
  raw=(unsigned char *)malloc(raw_size);
  decoded=(unsigned char *)malloc(decoded_size);
  if(!raw || !decoded) goto done;
  {
    uLongf output_size=(uLongf)raw_size;
    int zr=uncompress((Bytef *)raw,&output_size,(const Bytef *)idat,(uLong)idat_size);
    if(zr!=Z_OK || output_size!=(uLongf)raw_size) goto done;
  }
  {
    unsigned int y,x;
    for(y=0;y<height;y++) {
      const unsigned char *src=raw+(size_t)y*(row_bytes+1u)+1u;
      unsigned char filter=raw[(size_t)y*(row_bytes+1u)];
      unsigned char *dst=decoded+(size_t)y*row_bytes;
      const unsigned char *prev=y?decoded+(size_t)(y-1u)*row_bytes:NULL;
      if(filter>4u) goto done;
      for(x=0;x<row_bytes;x++) {
        int left=x>=bytes_per_pixel?dst[x-bytes_per_pixel]:0;
        int up=prev?prev[x]:0;
        int up_left=(prev && x>=bytes_per_pixel)?prev[x-bytes_per_pixel]:0;
        int predictor=0;
        if(filter==1u) predictor=left;
        else if(filter==2u) predictor=up;
        else if(filter==3u) predictor=(left+up)/2;
        else if(filter==4u) predictor=png_paeth(left,up,up_left);
        dst[x]=(unsigned char)(src[x]+predictor);
      }
    }
  }
  memset(cover_pixels,0,sizeof(cover_pixels));
  {
    unsigned int y,x;
    for(y=0;y<height;y++) {
      const unsigned char *row=decoded+(size_t)y*row_bytes;
      for(x=0;x<width;x++) {
        unsigned int r,g,b,a=255u;
        if(color_type==0u) {
          r=g=b=row[x];
        } else if(color_type==2u) {
          r=row[x*3u]; g=row[x*3u+1u]; b=row[x*3u+2u];
        } else if(color_type==4u) {
          r=g=b=row[x*2u]; a=row[x*2u+1u];
        } else if(color_type==6u) {
          r=row[x*4u]; g=row[x*4u+1u]; b=row[x*4u+2u]; a=row[x*4u+3u];
        } else {
          unsigned int index=row[x];
          if(index>=palette_entries) goto done;
          r=palette[index*3u]; g=palette[index*3u+1u]; b=palette[index*3u+2u];
          if(index<palette_alpha_count) a=palette_alpha[index];
        }
        if(a<128u) cover_pixels[(size_t)y*COVER_TEX_SIZE+x]=0;
        else cover_pixels[(size_t)y*COVER_TEX_SIZE+x]=(uint16_t)(
          0x8000u | ((r>>3)&31u) | (((g>>3)&31u)<<5) | (((b>>3)&31u)<<10));
      }
    }
  }
  ok=1;
done:
  if(idat) free(idat);
  if(raw) free(raw);
  if(decoded) free(decoded);
  return ok;
}
static int try_cover_path(const char *path) {
  unsigned char *data=NULL;
  size_t size=0;
  int r=read_cover_file(path,&data,&size);
  if(r<0) {
    if(r!=-1) cover_error=1;
    return 0;
  }
  r=decode_cover_png_200(data,size);
  free(data);
  if(!r) { cover_error=1; return 0; }
  if(!cover_tex_ready) { cover_error=1; return 0; }
  gsKit_texture_upload(gs,&cover_tex);
  cover_loaded=1;
  return 1;
}
static void normalize_cover_key(const char *input, char *out, size_t cap) {
  char temp[256];
  const char *name, *p;
  char *dot;
  size_t n, used=0;
  if(!out || !cap) return;
  out[0]='\0';
  if(!input) return;
  name=strrchr(input,'/');
  name=name?name+1:input;
  strncpy(temp,name,sizeof(temp)-1);
  temp[sizeof(temp)-1]='\0';
  dot=strrchr(temp,'.');
  if(dot) *dot='\0';
  while(temp[0] && isspace((unsigned char)temp[0]))
    memmove(temp,temp+1,strlen(temp));
  n=strlen(temp);
  while(n && isspace((unsigned char)temp[n-1])) temp[--n]='\0';
  /* ROM dumps often have a leading "R " marker that cover filenames omit. */
  if((temp[0]=='R' || temp[0]=='r') &&
     (temp[1]==' ' || temp[1]=='_' || temp[1]=='\t')) {
    memmove(temp,temp+2,strlen(temp+2)+1);
  }
  for(p=temp; *p && used+1<cap; ++p) {
    unsigned char ch=(unsigned char)*p;
    /* Ignore separators so spaces, underscores and hyphens all match. */
    if(isalnum(ch)) out[used++]=(char)toupper(ch);
  }
  out[used]='\0';
}
static int try_cover_in_dir(const char *dir,const char *base) {
  int d,r;
  iox_dirent_t image_entry __attribute__((aligned(64)));
  char wanted[256],candidate[256],path[MAX_PATH],matched[256];
  int n;
  matched[0]='\0';
  normalize_cover_key(base,wanted,sizeof(wanted));
  if(!wanted[0]) {
    strncpy(cover_debug,"EMPTY COVER KEY",sizeof(cover_debug)-1);
    cover_debug[sizeof(cover_debug)-1]='\0';
    return 0;
  }
  d=fileXioDopen(dir);
  if(d<0) {
    snprintf(cover_debug,sizeof(cover_debug),"DIR OPEN FAIL");
    return 0;
  }
  for(;;) {
    memset(&image_entry,0,sizeof(image_entry));
    r=fileXioDread(d,&image_entry);
    if(r<=0) break;
    if(FIO_S_ISDIR(image_entry.stat.mode)) continue;
    if(!has_extension_ci(image_entry.name,".png")) continue;
    normalize_cover_key(image_entry.name,candidate,sizeof(candidate));
    if(candidate[0] && strcmp(wanted,candidate)==0) {
      strncpy(matched,image_entry.name,sizeof(matched)-1);
      matched[sizeof(matched)-1]='\0';
      break;
    }
  }
  fileXioDclose(d);
  if(!matched[0]) {
    snprintf(cover_debug,sizeof(cover_debug),"NO MATCH: %.60s",wanted);
    return 0;
  }
  n=snprintf(path,sizeof(path),"%s%s",dir,matched);
  if(n<0 || n>=(int)sizeof(path)) {
    strncpy(cover_debug,"COVER PATH TOO LONG",sizeof(cover_debug)-1);
    cover_debug[sizeof(cover_debug)-1]='\0';
    return 0;
  }
  if(try_cover_path(path)) {
    snprintf(cover_debug,sizeof(cover_debug),"PNG FOUND");
    return 1;
  }
  cover_error=1;
  snprintf(cover_debug,sizeof(cover_debug),"PNG INVALID/UNSUPPORTED");
  return 0;
}
static int load_selected_cover(void) {
  char base[256],root_dir[MAX_PATH];
  const char *name,*slash,*dot,*root;
  size_t n;
  if(nentries<=0 || selected<0 || selected>=nentries || entries[selected].dir) return 0;
  if(!is_supported_rom(entries[selected].name)) return 0;
  name=entries[selected].name;
  slash=strrchr(name,'/');
  if(slash) name=slash+1;
  strncpy(base,name,sizeof(base)-1);
  base[sizeof(base)-1]='\0';
  dot=strrchr(base,'.');
  if(dot) *((char *)dot)='\0';
  n=strlen(base);
  while(n && (base[n-1]==' ' || base[n-1]=='.')) base[--n]='\0';
  if(!n) return 0;
  root=!strncmp(folder,"mass0:/",7)?"mass0:/":"mass:/";
  {
    int nr=snprintf(root_dir,sizeof(root_dir),"%sFCEUmm/images/",root);
    if(nr<0 || nr>=(int)sizeof(root_dir)) return 0;
  }
  if(try_cover_in_dir(root_dir,base)) return 1;
  return 0;
}
static void update_cover_for_selection(void) {
  char key[MAX_PATH];
  int n;
  if(nentries<=0 || selected<0 || selected>=nentries) {
    cover_key[0]='\0'; cover_loaded=0; cover_error=0;
    strncpy(cover_debug,"NO GAME SELECTED",sizeof(cover_debug)-1);
    cover_debug[sizeof(cover_debug)-1]='\0';
    return;
  }
  n=snprintf(key,sizeof(key),"%s%s",folder,entries[selected].name);
  if(n<0 || n>=(int)sizeof(key)) {
    cover_loaded=0; cover_error=1; cover_key[0]='\0'; return;
  }
  if(strcmp(key,cover_key)==0) return;
  strncpy(cover_key,key,sizeof(cover_key)-1);
  cover_key[sizeof(cover_key)-1]='\0';
  cover_loaded=0; cover_error=0;
  (void)load_selected_cover();
}
static void display_entry_title(int index,char *out,size_t cap) {
  const char *dot;
  const char *suffix=NULL;
  size_t n,i;
  char upper[256];
  if(!out || !cap) return;
  out[0]='\0';
  if(index<0 || index>=nentries) return;
  strncpy(out,entries[index].name,cap-1);
  out[cap-1]='\0';
  if(!entries[index].dir && is_supported_rom(out)) {
    dot=strrchr(out,'.');
    if(dot) out[(size_t)(dot-out)]='\0';
  }
  n=strlen(out);
  while(n && (out[n-1]==' ' || out[n-1]=='.')) out[--n]='\0';
  if(!entries[index].dir && (out[0]=='R' || out[0]=='r') &&
     (out[1]==' ' || out[1]=='_' || out[1]=='	'))
    memmove(out,out+2,strlen(out+2)+1);
  n=strlen(out);
  while(n && (out[n-1]==' ' || out[n-1]=='.')) out[--n]='\0';

  /* Friendly title for Splatterhouse, independent of original filename case. */
  for(i=0;i<n && i<sizeof(upper)-1;i++)
    upper[i]=(char)toupper((unsigned char)out[i]);
  upper[i]='\0';
  if(!entries[index].dir &&
     strstr(upper,"SPLATTERHOUSE") && strstr(upper,"JAPAN")) {
    strncpy(out,"Splatterhouse (Japan)",cap-1);
    out[cap-1]='\0';
  }

  /* Identify the file format in the menu without changing the actual path. */
  if(!entries[index].dir && is_nes(entries[index].name)) suffix=" [NES]";
  else if(!entries[index].dir && is_zip(entries[index].name)) suffix=" [ZIP]";
  else if(!entries[index].dir && is_supported_rom(entries[index].name)) suffix=" [NES]";
  if(suffix && strlen(out)<cap-1)
    strncat(out,suffix,cap-strlen(out)-1);
}
static void render_cover(void) {
  uint64_t aqua=GS_SETREG_RGBAQ(50,210,175,0x80,0);
  uint64_t frame=GS_SETREG_RGBAQ(50,210,175,0x80,0);
  uint64_t background=GS_SETREG_RGBAQ(12,20,35,0x80,0);
  gsKit_prim_sprite(gs,382.0f,147.0f,598.0f,363.0f,1,frame);
  gsKit_prim_sprite(gs,386.0f,151.0f,594.0f,359.0f,1,background);
  if(cover_loaded && cover_tex_ready) {
    gsKit_prim_sprite_texture(gs,&cover_tex,
      390.0f,155.0f,0.0f,0.0f,590.0f,355.0f,200.0f,200.0f,1,
      GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0));
  } else {
    const char *message="NO COVER";
    if(nentries>0 && selected>=0 && selected<nentries && entries[selected].dir)
      message="FOLDER";
    else if(cover_error) message="PNG ERROR";
    label(414,248,2,message,aqua);
  }
  label(382,370,1,cover_debug,aqua);
}

static void render_browser(void) {
  int first=selected-4,i;
  uint64_t white=GS_SETREG_RGBAQ(230,240,245,0x80,0);
  uint64_t aqua=GS_SETREG_RGBAQ(50,210,175,0x80,0);
  char msg[64];
  update_cover_for_selection();
  title_screen("SELECT NES GAME");
  if(first<0) first=0;
  if(first+9>nentries) first=nentries-9;
  if(first<0) first=0;
  for(i=first;i<nentries && i<first+9;i++) {
    display_entry_title(i,msg,sizeof(msg));
    label(24,154+(i-first)*27,2,msg,i==selected?aqua:white);
  }
  if(nentries==0) label(24,210,2,"NO NES OR ZIP FILES",white);
  render_cover();
  if(status_code<0) {
    snprintf(msg,sizeof(msg),"LOAD ERROR %d",status_code);
    label(24,390,1,msg,aqua);
  }
  label(24,415,1,"UP/DOWN HOLD L/R +/-10 X OPEN O BACK",aqua);
}
/* Removed: legacy hardcoded PCE cheat matching. */
/* Removed: legacy hardcoded PCE cheat matching. */
          static int text_equal_ci(const char *a,const char *b) {
  if(!a || !b) return 0;
  while(*a && *b) { if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++)) return 0; }
  return *a==0 && *b==0;
}
static char *trim_text(char *s) {
  char *end;
  while(*s && isspace((unsigned char)*s)) ++s;
  end=s+strlen(s);
  while(end>s && isspace((unsigned char)end[-1])) --end;
  *end='\0';
  return s;
}
static void copy_cht_value(char *dst,size_t cap,const char *src) {
  size_t n;
  char temp[FCEU_CHEAT_CODE];
  if(!cap) return;
  strncpy(temp,src,sizeof(temp)-1); temp[sizeof(temp)-1]='\0';
  { char *v=trim_text(temp); n=strlen(v);
    if(n>=2 && v[0]=='"' && v[n-1]=='"') { v[n-1]='\0'; ++v; }
    strncpy(dst,v,cap-1); dst[cap-1]='\0'; }
}
static int parse_cht_buffer(char *buffer) {
  FCEUCheat temp[FCEU_CHEAT_MAX];
  char *line;
  unsigned int max_index=0, i;
  int seen=0;
  memset(temp,0,sizeof(temp));
  line=strtok(buffer,"\r\n");
  while(line) {
    char *eq=strchr(line,'=');
    if(eq) {
      *eq='\0';
      char *key=trim_text(line), *value=trim_text(eq+1);
      if(!strncmp(key,"cheat",5)) {
        char *p=key+5, *endp;
        unsigned long idx=strtoul(p,&endp,10);
        if(endp!=p && idx<FCEU_CHEAT_MAX && *endp=='_') {
          FCEUCheat *c=&temp[idx]; c->source_index=(unsigned int)idx;
          if(!strcmp(endp,"_desc")) copy_cht_value(c->desc,sizeof(c->desc),value);
          else if(!strcmp(endp,"_code")) copy_cht_value(c->code,sizeof(c->code),value);
          else if(!strcmp(endp,"_enable")) c->enabled=(text_equal_ci(value,"true") || !strcmp(value,"1"));
          if(idx>max_index) max_index=(unsigned int)idx;
          seen=1;
        }
      }
    }
    line=strtok(NULL,"\r\n");
  }
  active_cheat_count=0;
  for(i=0;i<=max_index && i<FCEU_CHEAT_MAX;i++) {
    if(temp[i].desc[0] || temp[i].code[0]) {
      if(active_cheat_count<FCEU_CHEAT_MAX) fceu_cheats[active_cheat_count++]=temp[i];
    }
  }
  return seen ? (int)active_cheat_count : 0;
}
static int find_game_cht(char *out,size_t cap) {
  char dir[MAX_PATH], wanted[160], base[160];
  const char *source=game_member_path[0]?game_member_path:game_path;
  const char *leaf=strrchr(source,'/');
  const char *dot;
  int d,r;
  leaf=leaf?leaf+1:source; dot=strrchr(leaf,'.');
  if(!dot) return -1;
  if((size_t)(dot-leaf)>=sizeof(base)) return -2;
  memcpy(base,leaf,(size_t)(dot-leaf)); base[dot-leaf]='\0';
  if(snprintf(wanted,sizeof(wanted),"%s.cht",base)<0) return -3;
  if(snprintf(dir,sizeof(dir),"%sFCEUmm/cheats",!strncmp(game_path,"mass0:/",7)?"mass0:/":"mass:/")<0) return -4;
  d=fileXioDopen(dir); if(d<0) return -5;
  for(;;) {
    memset(&dirent_buf,0,sizeof(dirent_buf));
    r=fileXioDread(d,&dirent_buf); if(r<=0) break;
    dirent_buf.name[sizeof(dirent_buf.name)-1]='\0';
    if(FIO_S_ISDIR(dirent_buf.stat.mode) || !has_extension_ci(dirent_buf.name,".cht")) continue;
    if(text_equal_ci(dirent_buf.name,wanted)) {
      int n=snprintf(out,cap,"%s/%s",dir,dirent_buf.name);
      fileXioDclose(d);
      return (n>=0 && (size_t)n<cap)?0:-6;
    }
  }
  fileXioDclose(d); return -7;
}
static void load_game_cheats(void) {
  int fd,n,total=0;
  unsigned char *buffer;
  active_cheat_count=0; active_cht_path[0]='\0';
  memset(fceu_cheats,0,sizeof(fceu_cheats));
  if(find_game_cht(active_cht_path,sizeof(active_cht_path))<0) {
    state_set_message("NO NES CHT FILE"); return;
  }
  memset(&stat_buf,0,sizeof(stat_buf));
  if(fileXioGetStat(active_cht_path,&stat_buf)<0 || stat_buf.hisize || !stat_buf.size || stat_buf.size>65536u) {
    state_set_message("CHT FILE SIZE ERROR"); return;
  }
  buffer=(unsigned char *)malloc((size_t)stat_buf.size+1u);
  if(!buffer) { state_set_message("CHT OUT OF MEMORY"); return; }
  fd=fileXioOpen(active_cht_path,FIO_O_RDONLY);
  if(fd<0) { free(buffer); state_set_message("CHT OPEN ERROR"); return; }
  while(total<(int)stat_buf.size) {
    int want=(int)stat_buf.size-total; if(want>(int)sizeof(readbuf)) want=(int)sizeof(readbuf);
    n=fileXioRead(fd,readbuf,want);
    if(n<=0 || n>want) { fileXioClose(fd); free(buffer); state_set_message("CHT READ ERROR"); return; }
    memcpy(buffer+total,readbuf,(size_t)n); total+=n;
  }
  fileXioClose(fd); buffer[total]='\0';
  n=parse_cht_buffer((char *)buffer); free(buffer);
  if(n>0) {
    apply_enabled_cheats();
    state_set_message("NES CHT LOADED");
  } else {
    state_set_message("EMPTY OR UNKNOWN CHT");
  }
}
          /* Core-dependent cheat application is intentionally absent in Interface Edition. */
          /* Core-dependent cheat application is intentionally absent in Interface Edition. */
          /* Apply validated HuC6280 RAM writes directly. The tested PS2 build must
             Actual cheat application is deferred until the FCEUmm core is integrated. */
          /* Core-dependent cheat application is intentionally absent in Interface Edition. */
          static void disable_all_cheats(void) {
  unsigned int i;
  for(i=0;i<active_cheat_count;i++) fceu_cheats[i].enabled=0;
  fceumm_core_clear_cheats();
  state_set_message("ALL NES CHEATS OFF");
}
static void apply_enabled_cheats(void) {
  unsigned int i;
  unsigned int applied=0, unsupported=0;
  fceumm_core_clear_cheats();
  for(i=0;i<active_cheat_count;i++) {
    if(!fceu_cheats[i].enabled) continue;
    if(fceumm_core_add_cheat(fceu_cheats[i].desc[0]?fceu_cheats[i].desc:"NES CHEAT",
                             fceu_cheats[i].code))
      ++applied;
    else
      ++unsupported;
  }
  if(applied)
    state_set_message(unsupported?"CHEATS APPLIED - SOME UNSUPPORTED":"NES CHEATS APPLIED");
  else if(unsupported)
    state_set_message("UNSUPPORTED NES CHEAT CODE");
  else
    state_set_message("NO ACTIVE CHEATS");
}
          static void unload_game(void) {
  interface_session_active=0;
  fceumm_core_clear_cheats();
  fceumm_core_close_game();
  if(audio_ready) (void)audsrv_stop_audio();
  active_cheat_count=0; active_cht_path[0]='\0';
  game_member_path[0]='\0';
  memset(fceu_cheats,0,sizeof(fceu_cheats));
}
          static void state_set_message(const char *message) {
            if(!message) message = "";
            strncpy(state_message, message, sizeof(state_message)-1);
            state_message[sizeof(state_message)-1] = 0;
            state_message_frames = state_message[0] ? 180 : 0;
          }
static int state_save_slot(int slot) {
  int r;
  if(!interface_session_active || !fceumm_core_is_loaded()) {
    state_set_message("NO GAME LOADED");
    return -1;
  }
  r=fceumm_core_save_state(slot);
  state_set_message(r==0?"REAL STATE SAVED":"STATE SAVE FAILED");
  return r;
}
static int state_load_slot(int slot) {
  int r;
  if(!interface_session_active || !fceumm_core_is_loaded()) {
    state_set_message("NO GAME LOADED");
    return -1;
  }
  r=fceumm_core_load_state(slot);
  state_set_message(r==0?"REAL STATE LOAD REQUESTED":"STATE SLOT NOT FOUND");
  return r;
}
static void render_state_menu(void) {
            int i;
            char item[48];
            uint64_t white=GS_SETREG_RGBAQ(230,240,245,0x80,0);
            uint64_t aqua=GS_SETREG_RGBAQ(50,210,175,0x80,0);
            title_screen("SAVE / LOAD STATE");
            label(30,119,1,"UP DOWN X SELECT O BACK",aqua);
            for(i=0;i<13;i++) {
              if(i<FCEU_STATE_SLOTS) snprintf(item,sizeof(item),"SAVE SLOT %d",i+1);
              else if(i<FCEU_STATE_SLOTS*2) snprintf(item,sizeof(item),"LOAD SLOT %d",i-FCEU_STATE_SLOTS+1);
              else if(i==10) snprintf(item,sizeof(item),"RETURN TO GAME");
              else if(i==11) snprintf(item,sizeof(item),"NES CHEAT MANAGER");
              else snprintf(item,sizeof(item),"EXIT TO ROM BROWSER");
              label(54,142+i*20,2,item,i==state_menu_selected?aqua:white);
            }
            if(state_message_frames>0 && state_message[0]) label(20,420,1,state_message,aqua);
          }
          static void state_menu_activate(void) {
            if(state_menu_selected<FCEU_STATE_SLOTS) {
              (void)state_save_slot(state_menu_selected);
            } else if(state_menu_selected<FCEU_STATE_SLOTS*2) {
              (void)state_load_slot(state_menu_selected-FCEU_STATE_SLOTS);
            } else if(state_menu_selected==10) {
              screen=2; buttons_prev=buttons_now; buttons_edge=0;
            } else if(state_menu_selected==11) {
              screen=4; cheat_menu_selected=0;
              state_set_message(active_cheat_count?"CHTS FOUND":"NO CHT FOR GAME");
              buttons_prev=buttons_now; buttons_edge=0;
            } else {
              unload_game(); screen=1; buttons_prev=buttons_now; buttons_edge=0;
            }
          }
          static void cheat_menu_activate(void) {
  if(cheat_menu_selected<(int)active_cheat_count) {
    FCEUCheat *cheat=&fceu_cheats[cheat_menu_selected];
    cheat->enabled=!cheat->enabled;
    apply_enabled_cheats();
  } else if(cheat_menu_selected==(int)active_cheat_count) {
    disable_all_cheats();
  } else { screen=3; buttons_prev=buttons_now; buttons_edge=0; }
}
          static void render_cheat_menu(void) {
  int i,first; char item[80];
  uint64_t white=GS_SETREG_RGBAQ(230,240,245,0x80,0);
  uint64_t aqua=GS_SETREG_RGBAQ(50,210,175,0x80,0);
  title_screen("NES CHEAT MANAGER");
  label(12,120,1,"UP DOWN X TOGGLE O BACK",aqua);
  if(active_cheat_count==0) label(12,136,1,"NO MATCHING NES CHT FILE",aqua);
  else { snprintf(item,sizeof(item),"CHT ENTRIES: %u",active_cheat_count); label(12,136,1,item,aqua); }
  first=cheat_menu_selected-4; if(first<0) first=0;
  if(first>FCEU_CHEAT_MENU_ITEMS-9) first=FCEU_CHEAT_MENU_ITEMS-9;
  if(first<0) first=0;
  for(i=first;i<FCEU_CHEAT_MENU_ITEMS && i<first+9;i++) {
    if(i<(int)active_cheat_count) {
      FCEUCheat *cheat=&fceu_cheats[i];
      snprintf(item,sizeof(item),"%s %s",cheat->enabled?"ON":"OFF",cheat->desc[0]?cheat->desc:cheat->code);
    } else if(i==(int)active_cheat_count) snprintf(item,sizeof(item),"DISABLE ALL CHEATS");
    else snprintf(item,sizeof(item),"BACK TO TEST MENU");
    label(12,158+(i-first)*26,1,item,i==cheat_menu_selected?aqua:white);
  }
  if(state_message_frames>0 && state_message[0]) label(12,400,1,state_message,aqua);
  label(12,420,1,"X TO APPLY / O TO RETURN",aqua);
}
          static void input_poll(void) {
            if(turbo_updated_this_frame) return;
            turbo_updated_this_frame=1;
            if(buttons_now & PAD_CIRCLE) {
              turbo_i_active=((turbo_i_counter % FCEU_TURBO_PERIOD_FRAMES)<FCEU_TURBO_ON_FRAMES);
              turbo_i_counter++;
            } else { turbo_i_counter=0; turbo_i_active=0; }
            if(buttons_now & PAD_TRIANGLE) {
              turbo_ii_active=((turbo_ii_counter % FCEU_TURBO_PERIOD_FRAMES)<FCEU_TURBO_ON_FRAMES);
              turbo_ii_counter++;
            } else { turbo_ii_counter=0; turbo_ii_active=0; }
          }
          static void render_load_debug(void) {
  uint64_t white=GS_SETREG_RGBAQ(230,240,245,0x80,0);
  uint64_t aqua=GS_SETREG_RGBAQ(50,210,175,0x80,0);
  char line[160];
  title_screen("NES ROM VALIDATION");
  snprintf(line,sizeof(line),"LOAD STATUS: %d",status_code); label(24,112,2,line,aqua);
  snprintf(line,sizeof(line),"FILE: %.100s",game_path); label(24,151,1,line,white);
  snprintf(line,sizeof(line),"ROM BYTES: %lu",last_rom_size); label(24,181,1,line,aqua);
  label(24,211,1,"FCEUMM CORE LOAD FAILED",white);
  label(24,241,1,"ROM HEADER / ZIP CHECK FAILED OR PASSED",white);
  label(24,420,1,"CIRCLE: BACK TO USB BROWSER",aqua);
}
static int begin_game(const char *path) {
  int r;
  last_rom_size=0;
  unload_game();

  memset(&stat_buf,0,sizeof(stat_buf));
  if(fileXioGetStat(path,&stat_buf)>=0 && !stat_buf.hisize)
    last_rom_size=(unsigned long)stat_buf.size;

  r=fceumm_core_init(gs);
  if(r<0) return -300+r;

  r=audio_init(22050u);
  if(r<0) return -320+r;
  r=fceumm_core_set_audio_rate(audio_rate);
  if(r<0) return -330+r;

  r=fceumm_core_load_game(path);
  if(r<0) return -340+r;

  strncpy(game_member_path,path,sizeof(game_member_path)-1);
  game_member_path[sizeof(game_member_path)-1]='\0';
  interface_session_active=1;
  load_game_cheats();
  return 0;
}
static void draw_game(void) {
  uint32_t nes_buttons=0;
  int r;

  /* DualShock -> standard NES controller bit order used by FCEUmm pads.c. */
  if(buttons_now & PAD_CROSS)  nes_buttons |= 0x01u; /* A */
  if(buttons_now & PAD_SQUARE) nes_buttons |= 0x02u; /* B */
  if(buttons_now & PAD_SELECT) nes_buttons |= 0x04u;
  if(buttons_now & PAD_START)  nes_buttons |= 0x08u;
  if(buttons_now & PAD_UP)     nes_buttons |= 0x10u;
  if(buttons_now & PAD_DOWN)   nes_buttons |= 0x20u;
  if(buttons_now & PAD_LEFT)   nes_buttons |= 0x40u;
  if(buttons_now & PAD_RIGHT)  nes_buttons |= 0x80u;

  /* Existing frontend turbo buttons remain Circle (A) and Triangle (B). */
  if(turbo_i_active)  nes_buttons |= 0x01u;
  if(turbo_ii_active) nes_buttons |= 0x02u;

  r=fceumm_core_run_frame(nes_buttons);
  if(r<0) {
    title_screen("FCEUMM FRAME ERROR");
    label(24,220,2,"CORE FRAME FAILED",GS_SETREG_RGBAQ(255,90,90,0x80,0));
  }
}
          int main(int argc,char **argv) {
            (void)argc;(void)argv;
            gs=gsKit_init_global();
            if(!gs)return 1;
            gs->PSM=GS_PSM_CT16;
            gs->PSMZ=GS_PSMZ_16;
            gs->ZBuffering=GS_SETTING_OFF;
            dmaKit_init(D_CTRL_RELE_OFF,D_CTRL_MFD_OFF,D_CTRL_STS_UNSPEC,
                        D_CTRL_STD_OFF,D_CTRL_RCYC_8,1<<DMA_CHANNEL_GIF);
            dmaKit_chan_init(DMA_CHANNEL_GIF);
            gsKit_init_screen(gs);
            gsKit_mode_switch(gs,GS_ONESHOT);
            memset(&cover_tex,0,sizeof(cover_tex));
            cover_tex.Width=COVER_TEX_SIZE;
            cover_tex.Height=COVER_TEX_SIZE;
            cover_tex.PSM=GS_PSM_CT16;
            cover_tex.Mem=(u32 *)cover_pixels;
            cover_tex.Vram=gsKit_vram_alloc(gs,gsKit_texture_size(COVER_TEX_SIZE,COVER_TEX_SIZE,GS_PSM_CT16),GSKIT_ALLOC_USERBUFFER);
            cover_tex_ready=(cover_tex.Vram!=0);
            (void)init_embedded_background();
            SifInitRpc(0);
            /* Keep the PS2 ROM module pair used by the current frontend. */
            SifLoadModule("rom0:XSIO2MAN",0,NULL);
            SifLoadModule("rom0:XPADMAN",0,NULL);
            pad_init_result=padInit(0);
            pad_open_result=0;
            if(pad_init_result==1)
              pad_open_result=padPortOpen(0,0,pad_buf);
            pad_ready=(pad_open_result!=0);
            screen=0;
            for(;;) {
              char tmp[80];
              pad_scan();
              if(screen==0) {
                title_screen("FCEUMM INTERFACE EDITION");
                label(52,210,3,"X OPEN USB ROMS",GS_SETREG_RGBAQ(230,240,245,0x80,0));
                if(status_code<0) {
                  snprintf(tmp,sizeof(tmp),"ERROR %d",status_code);
                  label(52,299,2,tmp,GS_SETREG_RGBAQ(230,240,245,0x80,0));
                }
                if(buttons_edge&PAD_CROSS) {
                  status_code=usb_init();
                  if(status_code>=0) {
                    usb_progress("SCAN USB ROOT");
                    strcpy(folder,"mass:/FCEUmm/nes roms/");
                    status_code=scan_dir();
                    if(status_code<0) {
                      usb_progress("SCAN MASS0 NES ROMS");
                      strcpy(folder,"mass0:/FCEUmm/nes roms/");
                      status_code=scan_dir();
                    }
                    if(status_code>=0) screen=1;
                  }
                }
                refresh_screen();
              } else if(screen==1) {
                if(nentries>0) {
                  if(nav_repeat(PAD_UP)) selected=(selected+nentries-1)%nentries;
                  if(nav_repeat(PAD_DOWN)) selected=(selected+1)%nentries;
                  if(buttons_edge&PAD_LEFT) {
                    selected=(selected>=10)?(selected-10):0;
                  }
                  if(buttons_edge&PAD_RIGHT) {
                    selected=(selected+10<nentries)?(selected+10):(nentries-1);
                  }
                }
                if(buttons_edge&PAD_CIRCLE)enter_parent();
                if((buttons_edge&PAD_CROSS) && nentries>0) {
                  char next[MAX_PATH];
                  int written=snprintf(next,sizeof(next),"%s%s%s",folder,
                    entries[selected].name,entries[selected].dir?"/":"");
                  if(written<0 || written>=(int)sizeof(next)) status_code=-105;
                  else if(entries[selected].dir) {
                    strcpy(folder,next);status_code=scan_dir();
                  } else {
                    strcpy(game_path,next);
                    status_code=begin_game(game_path);
                    if(status_code>=0)screen=2;
                    else screen=5;
                  }
                }
                render_browser();refresh_screen();
              } else if(screen==5) {
                if(buttons_edge&PAD_CIRCLE) {
                  screen=1; buttons_prev=buttons_now; buttons_edge=0;
                }
                render_load_debug(); refresh_screen();
              } else if(screen==3) {
                if(buttons_edge&PAD_UP) state_menu_selected=(state_menu_selected+12)%13;
                if(buttons_edge&PAD_DOWN) state_menu_selected=(state_menu_selected+1)%13;
                if(buttons_edge&PAD_CIRCLE) {
                  screen=2; buttons_prev=buttons_now; buttons_edge=0;
                } else if(buttons_edge&PAD_CROSS) {
                  state_menu_activate();
                }
                render_state_menu(); refresh_screen();
                if(state_message_frames>0) state_message_frames--;
              } else if(screen==4) {
                if(buttons_edge&PAD_UP) cheat_menu_selected=(cheat_menu_selected+FCEU_CHEAT_MENU_ITEMS-1)%FCEU_CHEAT_MENU_ITEMS;
                if(buttons_edge&PAD_DOWN) cheat_menu_selected=(cheat_menu_selected+1)%FCEU_CHEAT_MENU_ITEMS;
                if(buttons_edge&PAD_CIRCLE) {
                  screen=3; buttons_prev=buttons_now; buttons_edge=0;
                } else if(buttons_edge&PAD_CROSS) {
                  cheat_menu_activate();
                }
                render_cheat_menu(); refresh_screen();
                if(state_message_frames>0) state_message_frames--;
              } else {
                if((buttons_now&(PAD_L1|PAD_R1|PAD_START))==(PAD_L1|PAD_R1|PAD_START)) {
                  unload_game();screen=1;
                  buttons_prev=buttons_now;
                  continue;
                }
                /* L1 + R1 + SELECT opens Save/Load State; Turbo Circle/Triangle stay untouched. */
                if((buttons_now&(PAD_L1|PAD_R1|PAD_SELECT))==(PAD_L1|PAD_R1|PAD_SELECT)) {
                  screen=3; state_menu_selected=0;
                  state_set_message("SELECT SAVE OR LOAD SLOT");
                  buttons_prev=buttons_now; buttons_edge=0;
                  continue;
                }
                /* Debug-only counters are omitted from the performance build. */
          #if FCEU_SHOW_DIAGNOSTICS
                input_active_mask=0;
                input_query_mask=0;
                input_mask_query_count=0;
                input_poll_count=0;
                input_state_count=0;
                input_last_port=-1;
                input_last_device=-1;
                input_last_id=-1;
                input_last_value=0;
                input_a_queries=0;
                input_b_queries=0;
                input_a_value=0;
                input_b_value=0;
          #endif
                /* Run one real FCEUmm emulation frame. */
                turbo_updated_this_frame=0;
                input_poll();
                draw_game();
                if(state_message_frames>0 && state_message[0]) {
                  label(12,430,1,state_message,GS_SETREG_RGBAQ(50,210,175,0x80,0));
                  state_message_frames--;
                }
                refresh_screen();
              }
            }
            return 0;
          }

