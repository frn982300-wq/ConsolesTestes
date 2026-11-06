          /*
           * FCEUmm PS2 - interface-only test edition.
           * Reuses the tested PCE Fast frontend shell; no emulation core is linked.
           * No emulation core is compiled, linked, or loaded in this edition.
           * No ROM or firmware bundled. Uses the previously tested V10/V11 exFAT driver pair.
           * audsrv is initialized for a driver check; no emulation audio is generated.
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
          /* Some PS2SDK/newlib headers do not expose snprintf under the current
             feature macros. Declare its standard signature explicitly. */
          extern int snprintf(char *str, size_t size, const char *format, ...);

          #define MAX_FILES 128
          #define MAX_PATH 512
          #define ROM_LIMIT (8u * 1024u * 1024u)
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
          /* ROM validation details shown in the interface-only test screen. */
          static unsigned long last_rom_size;
                              static unsigned char *game_data;
          static size_t game_size;
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
          static unsigned audio_rate = 44100;

          /* Interface-test slots verify USB read/write and CRC only; no emulation state exists yet. */
          #define FCEU_STATE_SLOTS 5
          #define FCEU_STATE_VERSION 1u
          typedef struct {
            char magic[8];
            uint32_t version;
            uint32_t game_hash;
            uint32_t payload_size;
            uint32_t payload_hash;
            uint32_t turbo_i_counter;
            uint32_t turbo_ii_counter;
          } FCEUmmTestStateHeader;
          static int state_menu_selected = 0;
          static char state_message[64] = "";
          static int state_message_frames;

          /* External NES .cht files are parsed for interface testing only. No code is applied without FCEUmm. */
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
            struct audsrv_fmt_t format;

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

            /*
             * Current PS2SDK audsrv API takes a pointer to audsrv_fmt_t.
             * Keep the existing requested-rate fallback order.
             */
            freq=(int)(requested_rate ? requested_rate : 44100);
            format.freq=freq;
            format.bits=16;
            format.channels=2;
            r=audsrv_set_format(&format);

            if(r<0 && freq!=44100) {
              freq=44100;
              format.freq=freq;
              r=audsrv_set_format(&format);
            }
            if(r<0 && freq!=22050) {
              freq=22050;
              format.freq=freq;
              r=audsrv_set_format(&format);
            }
            if(r<0) { audio_error=r; return r; }

            r=audsrv_set_volume(90);
            if(r<0) { audio_error=r; return r; }
            audio_rate=(unsigned)freq;
            audio_ready=1; audio_error=0;
            return 0;
          }
          /* No video-frame conversion is needed before the core is integrated. */
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
              if(!is_nes(dirent_buf.name) && !is_zip(dirent_buf.name)) continue;
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
  if(!is_nes(entries[selected].name) && !is_zip(entries[selected].name)) return 0;
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
  if(!entries[index].dir && (is_nes(out) || is_zip(out))) {
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
  state_set_message(n>0?"NES CHT LOADED":"EMPTY OR UNKNOWN CHT");
}
          /* Core-dependent cheat application is intentionally absent in Interface Edition. */
          /* Core-dependent cheat application is intentionally absent in Interface Edition. */
          /* Apply validated HuC6280 RAM writes directly. The tested PS2 build must
             Actual cheat application is deferred until the FCEUmm core is integrated. */
          /* Core-dependent cheat application is intentionally absent in Interface Edition. */
          static void disable_all_cheats(void) {
  unsigned int i; for(i=0;i<active_cheat_count;i++) fceu_cheats[i].enabled=0;
  state_set_message("ALL NES CHEATS OFF");
}
          static void unload_game(void) {
  interface_session_active=0;
  active_cheat_count=0; active_cht_path[0]='\0';
  if(game_data) { free(game_data); game_data=NULL; }
  game_size=0; game_member_path[0]='\0';
  memset(fceu_cheats,0,sizeof(fceu_cheats));
}
          static uint16_t zip_u16(const unsigned char *p) {
            return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
          }
          static uint32_t zip_u32(const unsigned char *p) {
            return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
          }
          static int zip_member_is_nes(const unsigned char *name, size_t n) {
  if(!name || n<4) return 0;
  return name[n-4]=='.' &&
    tolower((unsigned char)name[n-3])=='n' &&
    tolower((unsigned char)name[n-2])=='e' &&
    tolower((unsigned char)name[n-1])=='s';
}
          /* Supports standard single-disk ZIP files with one .nes member, using
             stored (method 0) or raw DEFLATE (method 8). ZIP64/encrypted entries
             and multi-volume archives are deliberately rejected with an error code. */
          static int extract_nes_from_zip(const unsigned char *zip, size_t zip_size) {
  size_t min_eocd, eocd, cd_off, cd_size, cd_end, pos;
  uint16_t entries, i;
  if(!zip || zip_size<22) return -200;
  min_eocd=zip_size>65557u ? zip_size-65557u : 0u;
  eocd=zip_size-22u;
  for(;;) {
    if(zip_u32(zip+eocd)==0x06054b50u) break;
    if(eocd==min_eocd) return -201;
    --eocd;
  }
  if(eocd+22u>zip_size || eocd+22u+(size_t)zip_u16(zip+eocd+20u)>zip_size) return -202;
  if(zip_u16(zip+eocd+4u)!=0 || zip_u16(zip+eocd+6u)!=0 ||
     zip_u16(zip+eocd+8u)!=zip_u16(zip+eocd+10u)) return -203;
  entries=zip_u16(zip+eocd+10u);
  cd_size=(size_t)zip_u32(zip+eocd+12u);
  cd_off=(size_t)zip_u32(zip+eocd+16u);
  if(entries==0xffffu || cd_size==0xffffffffu || cd_off==0xffffffffu) return -204;
  if(cd_off>zip_size || cd_size>zip_size-cd_off) return -205;
  cd_end=cd_off+cd_size;
  if(cd_end>eocd) return -206;
  pos=cd_off;
  for(i=0;i<entries;i++) {
    uint16_t flags, method, name_len, extra_len, comment_len, disk_start;
    uint32_t crc_expected, comp_size32, uncomp_size32, local_off32;
    size_t entry_len, local_off, data_off, comp_size, uncomp_size;
    const unsigned char *name;
    if(pos>cd_end || cd_end-pos<46u || zip_u32(zip+pos)!=0x02014b50u) return -207;
    flags=zip_u16(zip+pos+8u); method=zip_u16(zip+pos+10u);
    crc_expected=zip_u32(zip+pos+16u); comp_size32=zip_u32(zip+pos+20u);
    uncomp_size32=zip_u32(zip+pos+24u); name_len=zip_u16(zip+pos+28u);
    extra_len=zip_u16(zip+pos+30u); comment_len=zip_u16(zip+pos+32u);
    disk_start=zip_u16(zip+pos+34u); local_off32=zip_u32(zip+pos+42u);
    entry_len=46u+(size_t)name_len+(size_t)extra_len+(size_t)comment_len;
    if(entry_len>cd_end-pos) return -208;
    name=zip+pos+46u; pos+=entry_len;
    if(!zip_member_is_nes(name,name_len)) continue;
    if((flags&1u)!=0 || disk_start!=0) return -209;
    if(comp_size32==0xffffffffu || uncomp_size32==0xffffffffu || local_off32==0xffffffffu) return -210;
    comp_size=(size_t)comp_size32; uncomp_size=(size_t)uncomp_size32; local_off=(size_t)local_off32;
    if(!uncomp_size || uncomp_size>ROM_LIMIT || comp_size>ROM_LIMIT) return -211;
    if(local_off>zip_size || zip_size-local_off<30u || zip_u32(zip+local_off)!=0x04034b50u) return -212;
    data_off=local_off+30u+(size_t)zip_u16(zip+local_off+26u)+(size_t)zip_u16(zip+local_off+28u);
    if(data_off>zip_size || comp_size>zip_size-data_off) return -213;
    game_data=(unsigned char *)malloc(uncomp_size);
    if(!game_data) return -214;
    if(method==0) {
      if(comp_size!=uncomp_size) { free(game_data); game_data=NULL; return -215; }
      memcpy(game_data,zip+data_off,uncomp_size);
    } else if(method==8) {
      z_stream stream; int zr; memset(&stream,0,sizeof(stream));
      stream.next_in=(Bytef *)(zip+data_off); stream.avail_in=(uInt)comp_size;
      stream.next_out=(Bytef *)game_data; stream.avail_out=(uInt)uncomp_size;
      zr=inflateInit2(&stream,-MAX_WBITS);
      if(zr!=Z_OK) { free(game_data); game_data=NULL; return -216; }
      zr=inflate(&stream,Z_FINISH); inflateEnd(&stream);
      if(zr!=Z_STREAM_END || stream.total_out!=(uLong)uncomp_size) { free(game_data); game_data=NULL; return -217; }
    } else { free(game_data); game_data=NULL; return -218; }
    {
      uLong calculated_crc=crc32(0L,Z_NULL,0);
      calculated_crc=crc32(calculated_crc,(const Bytef *)game_data,(uInt)uncomp_size);
      if((uint32_t)calculated_crc!=crc_expected) { free(game_data); game_data=NULL; return -219; }
    }
    game_size=uncomp_size;
    if(name_len>=sizeof(game_member_path)) { free(game_data); game_data=NULL; game_size=0; return -220; }
    memcpy(game_member_path,name,name_len); game_member_path[name_len]='\0';
    return 0;
  }
  return -221;
}
          /* ZIP member extracted in memory for file validation; no core is loaded. */
/* ZIP temporary ROM writer omitted in interface-only build. */
static int read_full_game(const char *path) {
            int fd,n;
            size_t off=0, sz;
            unsigned char *file_data;
            memset(&stat_buf,0,sizeof(stat_buf));
            n=fileXioGetStat(path,&stat_buf);
            if(n<0)return n;
            if(stat_buf.hisize || !stat_buf.size || stat_buf.size>ROM_LIMIT)return -100;
            sz=(size_t)stat_buf.size;
            file_data=(unsigned char *)malloc(sz);
            if(!file_data)return -101;
            fd=fileXioOpen(path,FIO_O_RDONLY);
            if(fd<0){free(file_data);return fd;}
            if(fileXioLseek(fd,0,FIO_SEEK_SET)!=0) {
              fileXioClose(fd); free(file_data); return -102;
            }
            while(off<sz) {
              int want=(int)(sz-off);
              if(want>(int)sizeof(readbuf))want=(int)sizeof(readbuf);
              n=fileXioRead(fd,readbuf,want);
              if(n<=0 || n>want) {
                fileXioClose(fd); free(file_data); return n<0?n:-103;
              }
              memcpy(file_data+off,readbuf,(size_t)n);
              off+=(size_t)n;
            }
            n=fileXioClose(fd);
            if(n<0){free(file_data);return n;}
            if(is_zip(path)) {
              n=extract_nes_from_zip(file_data,sz);
              free(file_data);
              if(n<0) return n;
              return 0;
            }
            game_data=file_data;
            game_size=sz;
            strncpy(game_member_path,path,sizeof(game_member_path)-1);
            game_member_path[sizeof(game_member_path)-1]='\0';
            return 0;
          }
          static uint32_t state_hash_bytes(const unsigned char *data, size_t size) {
            uint32_t h = 2166136261u;
            size_t i;
            for(i=0;i<size;i++) { h ^= data[i]; h *= 16777619u; }
            return h;
          }
          static uint32_t state_game_hash(void) {
            const char *p = game_path;
            const char *colon = strchr(game_path, ':');
            uint32_t h = 2166136261u;
            if(colon && colon[1]=='/') p = colon + 2; /* Ignore mass:/ vs mass0:/ alias. */
            while(*p) {
              unsigned char c = (unsigned char)tolower((unsigned char)*p++);
              h ^= c; h *= 16777619u;
            }
            return h;
          }
          static void state_set_message(const char *message) {
            if(!message) message = "";
            strncpy(state_message, message, sizeof(state_message)-1);
            state_message[sizeof(state_message)-1] = 0;
            state_message_frames = state_message[0] ? 180 : 0;
          }
          static int state_make_paths(int slot, char *dir1, size_t dir1_cap,
                                      char *dir2, size_t dir2_cap,
                                      char *path, size_t path_cap) {
            const char *root;
            const char *base;
            const char *dot;
            char slug[64];
            size_t j=0;
            int n;
            if(slot<0 || slot>=FCEU_STATE_SLOTS || !game_path[0]) return -1;
            root = !strncmp(game_path,"mass0:/",7) ? "mass0:/" : "mass:/";
            n=snprintf(dir1,dir1_cap,"%sFCEUmm",root);
            if(n<0 || (size_t)n>=dir1_cap) return -2;
            n=snprintf(dir2,dir2_cap,"%sFCEUmm/STATES",root);
            if(n<0 || (size_t)n>=dir2_cap) return -3;
            base=strrchr(game_path,'/'); base=base?base+1:game_path;
            dot=strrchr(base,'.');
            while(*base && base!=dot && j<sizeof(slug)-1) {
              unsigned char c=(unsigned char)*base++;
              if(isalnum(c)) slug[j++]=(char)toupper(c);
              else if(j && slug[j-1]!='_') slug[j++]='_';
            }
            while(j && slug[j-1]=='_') --j;
            if(!j) { slug[0]='G'; slug[1]='A'; slug[2]='M'; slug[3]='E'; j=4; }
            slug[j]=0;
            n=snprintf(path,path_cap,"%sFCEUmm/STATES/%s_%08X.testslot%d",
                       root,slug,(unsigned int)state_game_hash(),slot+1);
            if(n<0 || (size_t)n>=path_cap) return -4;
            return 0;
          }
          static int state_ensure_directory(const char *path) {
            int d=fileXioDopen(path);
            if(d>=0) { fileXioDclose(d); return 0; }
            (void)fileXioMkdir(path,0777);
            d=fileXioDopen(path);
            if(d<0) return d;
            fileXioDclose(d);
            return 0;
          }
          static int state_ensure_directories(const char *dir1,const char *dir2) {
            if(state_ensure_directory(dir1)<0) return -1;
            if(state_ensure_directory(dir2)<0) return -2;
            return 0;
          }
          static int state_write_exact(int fd,const void *buffer,size_t size) {
            const unsigned char *p=(const unsigned char *)buffer;
            while(size) {
              int chunk=(size>(size_t)16384)?16384:(int)size;
              int n=fileXioWrite(fd,(void *)p,chunk);
              if(n<=0 || n>chunk) return -1;
              p+=(size_t)n; size-=(size_t)n;
            }
            return 0;
          }
          static int state_read_exact(int fd,void *buffer,size_t size) {
            unsigned char *p=(unsigned char *)buffer;
            while(size) {
              int chunk=(size>(size_t)16384)?16384:(int)size;
              int n=fileXioRead(fd,p,chunk);
              if(n<=0 || n>chunk) return -1;
              p+=(size_t)n; size-=(size_t)n;
            }
            return 0;
          }
          static int state_save_slot(int slot) {
  char dir1[MAX_PATH],dir2[MAX_PATH],path[MAX_PATH],tmp[MAX_PATH];
  const size_t size=64u;
  unsigned char *payload;
  FCEUmmTestStateHeader header;
  int fd,r;
  if(!interface_session_active) { state_set_message("NO TEST SESSION"); return -1; }
  payload=(unsigned char *)calloc(1,size);
  if(!payload) { state_set_message("TEST SLOT OUT OF MEMORY"); return -3; }
  memcpy(payload,"FCEUMM-UI-TEST",14);
  { uint32_t test_hash=state_game_hash(); memcpy(payload+16,&test_hash,sizeof(test_hash)); }
  memcpy(payload+20,&turbo_i_counter,sizeof(turbo_i_counter));
  memcpy(payload+24,&turbo_ii_counter,sizeof(turbo_ii_counter));
  r=state_make_paths(slot,dir1,sizeof(dir1),dir2,sizeof(dir2),path,sizeof(path));
  if(r<0 || state_ensure_directories(dir1,dir2)<0) { free(payload); state_set_message("STATE USB DIRECTORY ERROR"); return -5; }
  if(snprintf(tmp,sizeof(tmp),"%s.tmp",path)<0 || strlen(path)+4>=sizeof(tmp)) { free(payload); state_set_message("STATE PATH TOO LONG"); return -6; }
  memset(&header,0,sizeof(header)); memcpy(header.magic,"FCEUTEST",8);
  header.version=FCEU_STATE_VERSION; header.game_hash=state_game_hash();
  header.payload_size=(uint32_t)size; header.payload_hash=state_hash_bytes(payload,size);
  header.turbo_i_counter=turbo_i_counter; header.turbo_ii_counter=turbo_ii_counter;
  fd=fileXioOpen(tmp,FIO_O_WRONLY|FIO_O_CREAT|FIO_O_TRUNC);
  if(fd<0) { free(payload); state_set_message("STATE FILE OPEN ERROR"); return -7; }
  r=state_write_exact(fd,&header,sizeof(header)); if(r==0) r=state_write_exact(fd,payload,size);
  if(fileXioClose(fd)<0) r=-1;
  free(payload);
  if(r<0) { fileXioRemove(tmp); state_set_message("STATE WRITE ERROR"); return -8; }
  (void)fileXioRemove(path);
  if(fileXioRename(tmp,path)<0) { fileXioRemove(tmp); state_set_message("STATE RENAME ERROR"); return -9; }
  state_set_message("TEST SLOT WRITE OK"); return 0;
}
          static int state_load_slot(int slot) {
  char dir1[MAX_PATH],dir2[MAX_PATH],path[MAX_PATH];
  FCEUmmTestStateHeader header; unsigned char *payload; int fd,r;
  const size_t expected=64u;
  if(!interface_session_active) { state_set_message("NO TEST SESSION"); return -1; }
  r=state_make_paths(slot,dir1,sizeof(dir1),dir2,sizeof(dir2),path,sizeof(path));
  if(r<0) { state_set_message("STATE PATH ERROR"); return -3; }
  memset(&stat_buf,0,sizeof(stat_buf));
  if(fileXioGetStat(path,&stat_buf)<0 || stat_buf.hisize || stat_buf.size<sizeof(header)) { state_set_message("TEST SLOT EMPTY"); return -4; }
  fd=fileXioOpen(path,FIO_O_RDONLY); if(fd<0) { state_set_message("STATE OPEN ERROR"); return -5; }
  r=state_read_exact(fd,&header,sizeof(header));
  if(r<0) { fileXioClose(fd); state_set_message("STATE HEADER ERROR"); return -6; }
  if(memcmp(header.magic,"FCEUTEST",8)!=0 || header.version!=FCEU_STATE_VERSION ||
     header.game_hash!=state_game_hash() || header.payload_size!=expected ||
     (uint64_t)sizeof(header)+(uint64_t)header.payload_size!=(uint64_t)stat_buf.size) {
    fileXioClose(fd); state_set_message("TEST SLOT INCOMPATIBLE"); return -7;
  }
  payload=(unsigned char *)malloc(expected);
  if(!payload) { fileXioClose(fd); state_set_message("TEST SLOT OUT OF MEMORY"); return -8; }
  r=state_read_exact(fd,payload,expected); if(fileXioClose(fd)<0) r=-1;
  if(r<0 || state_hash_bytes(payload,expected)!=header.payload_hash || memcmp(payload,"FCEUMM-UI-TEST",14)!=0) {
    free(payload); state_set_message("TEST SLOT CRC ERROR"); return -9;
  }
  free(payload);
  turbo_i_counter=header.turbo_i_counter; turbo_ii_counter=header.turbo_ii_counter;
  turbo_i_active=0; turbo_ii_active=0; turbo_updated_this_frame=0;
  state_set_message("TEST SLOT READ OK"); return 0;
}
          static void render_state_menu(void) {
            int i;
            char item[48];
            uint64_t white=GS_SETREG_RGBAQ(230,240,245,0x80,0);
            uint64_t aqua=GS_SETREG_RGBAQ(50,210,175,0x80,0);
            title_screen("SAVE / LOAD STATE");
            label(30,119,1,"UP DOWN X SELECT O BACK",aqua);
            for(i=0;i<13;i++) {
              if(i<FCEU_STATE_SLOTS) snprintf(item,sizeof(item),"TEST SAVE SLOT %d",i+1);
              else if(i<FCEU_STATE_SLOTS*2) snprintf(item,sizeof(item),"TEST LOAD SLOT %d",i-FCEU_STATE_SLOTS+1);
              else if(i==10) snprintf(item,sizeof(item),"RETURN TO TEST SESSION");
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
    state_set_message(cheat->enabled?"CHEAT ON - UI ONLY":"CHEAT OFF");
  } else if(cheat_menu_selected==(int)active_cheat_count) {
    disable_all_cheats();
  } else { screen=3; buttons_prev=buttons_now; buttons_edge=0; }
}
          static void render_cheat_menu(void) {
  int i,first; char item[80];
  uint64_t white=GS_SETREG_RGBAQ(230,240,245,0x80,0);
  uint64_t aqua=GS_SETREG_RGBAQ(50,210,175,0x80,0);
  title_screen("NES CHEATS - TEST MODE");
  label(12,120,1,"UP DOWN X TOGGLE O BACK",aqua);
  if(active_cheat_count==0) label(12,136,1,"NO MATCHING NES CHT FILE",aqua);
  else { snprintf(item,sizeof(item),"CHT ENTRIES: %u - UI ONLY",active_cheat_count); label(12,136,1,item,aqua); }
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
  label(12,420,1,"TOGGLES ARE NOT APPLIED WITHOUT FCEUMM CORE",aqua);
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
  label(24,211,1,"NO CORE WAS STARTED",white);
  label(24,241,1,"ROM HEADER / ZIP CHECK FAILED OR PASSED",white);
  label(24,420,1,"CIRCLE: BACK TO USB BROWSER",aqua);
}
static int begin_game(const char *path) {
  int r;
  last_rom_size=0; 
  unload_game();
  r=read_full_game(path);
  if(r<0) return r;
  last_rom_size=(unsigned long)game_size;
  if(!game_data || game_size<16u || memcmp(game_data,"NES\x1a",4)!=0) { unload_game(); return -230; }
  
  interface_session_active=1;
  (void)audio_init(44100u);
  load_game_cheats();
  return 0;
}
static void draw_game(void) {
  char line[128];
  uint64_t white=GS_SETREG_RGBAQ(230,240,245,0x80,0);
  uint64_t aqua=GS_SETREG_RGBAQ(50,210,175,0x80,0);
  const char *base=strrchr(game_path,'/'); base=base?base+1:game_path;
  title_screen("INTERFACE TEST MODE");
  label(32,132,2,"NO EMULATION CORE LOADED",aqua);
  snprintf(line,sizeof(line),"ROM: %.72s",base); label(24,190,1,line,white);
  snprintf(line,sizeof(line),"SIZE: %lu BYTES",last_rom_size); label(24,220,1,line,white);
  label(24,260,1,audio_ready?"AUDSRV MODULE READY":"AUDSRV NOT READY",aqua);
  snprintf(line,sizeof(line),"TURBO I: %s  TURBO II: %s",turbo_i_active?"ON":"OFF",turbo_ii_active?"ON":"OFF");
  label(24,290,1,line,white);
  label(24,350,1,"L1+R1+SELECT: TEST STATES / CHEATS",aqua);
  label(24,380,1,"L1+R1+START: RETURN TO BROWSER",aqua);
  label(24,420,1,"CIRCLE / TRIANGLE: TEST TURBO INPUT",white);
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
                /* Interface-only test loop. */
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

