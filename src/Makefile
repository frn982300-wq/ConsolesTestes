# FCEUmm Interface Edition + FCEUmm-PS2 0.3.3
# The original PS2 core is compiled as the only emulator core.
# Frontend, background, USB/exFAT and artwork browser remain our tested code.

EE_BIN = FCEUmm-NES-PS2.ELF

PS2DEV ?= /usr/local/ps2dev
PS2SDK ?= $(PS2DEV)/ps2sdk
GSKIT ?= $(PS2DEV)/gsKit

GSKIT_INCLUDE ?= $(GSKIT)/include
GSKIT_LIBDIR ?= $(GSKIT)/lib
DMAKIT_LIBDIR ?= $(GSKIT)/lib
AUDSRV_INC ?= $(PS2SDK)/ports/include
AUDSRV_LIBDIR ?= $(PS2SDK)/ports/lib
FCEU_ZLIB_INCLUDE ?= $(PS2SDK)/ports/include
FCEU_ZLIB_LIB ?= $(PS2SDK)/ports/lib/libz.a
PS2_NEWLIB_SYS_TYPES ?= $(PS2DEV)/ee/mips64r5900el-ps2-elf/include/sys/types.h

CORE_DIR = core
BRIDGE_DIR = ps2bridge
CORE_SOURCE_DIRS = $(BRIDGE_DIR) $(CORE_DIR) $(CORE_DIR)/boards $(CORE_DIR)/input $(CORE_DIR)/mappers $(CORE_DIR)/zlib

FCEU_CORE_SRCS = $(foreach dir,$(CORE_SOURCE_DIRS),$(wildcard $(dir)/*.c))
FCEU_CORE_OBJS = $(FCEU_CORE_SRCS:.c=.o)

EE_OBJS = fceumm_interface.o $(FCEU_CORE_OBJS) usbd.o usbhdfsd.o iomanx.o filexio.o audsrv.o

EE_CFLAGS += -O1 -G0 -g -Wall -fno-omit-frame-pointer
EE_CFLAGS += -funsigned-char -fno-builtin-printf -DFCEUMM_DEBUG_TRACE=1
EE_CFLAGS += -D_EE -D__PS2__ -DHAVE_ASPRINTF
EE_CFLAGS += -DPATH_MAX=1024 -DINLINE=inline -DPSS_STYLE=1 -DFCEU_VERSION_NUMERIC=9813
EE_CFLAGS += -DLSB_FIRST -DLOCAL_LE=1
EE_CFLAGS += -DNEWLIB_PORT_AWARE -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D__SYS_TIME_H__
EE_CFLAGS += -include $(PS2_NEWLIB_SYS_TYPES)
EE_CFLAGS += -I. -I$(CORE_DIR)/zlib -I$(PS2SDK)/ports/include
EE_CFLAGS += -I$(PS2SDK)/ee/include -I$(PS2SDK)/sbv/include -I$(PS2SDK)/ports/include
EE_CFLAGS += -I$(PS2SDK)/common/include -I$(GSKIT_INCLUDE) -I$(AUDSRV_INC) -I$(FCEU_ZLIB_INCLUDE)
EE_CFLAGS += -I$(GSKIT)/ee/dma/include -I$(GSKIT)/ee/gs/include -I$(GSKIT)/ee/toolkit/include

EE_LDFLAGS += -L$(PS2SDK)/ee/lib -L$(GSKIT_LIBDIR) -L$(DMAKIT_LIBDIR)
EE_LDFLAGS += -L$(PS2SDK)/ports/lib -L$(AUDSRV_LIBDIR)
EE_LIBS += -laudsrv -lgskit_toolkit -lgskit -ldmakit -lpad -lfileXio -lpatches
EE_LIBS += $(FCEU_ZLIB_LIB) -lm -lstdc++

all: $(EE_BIN)


%.o: %.c
	$(EE_CC) $(EE_CFLAGS) -c $< -o $@

usbd.c: ../assets/usbd.irx
	bin2c $< $@ usbd_irx

usbhdfsd.c: ../assets/usbhdfsd.irx
	bin2c $< $@ usbhdfsd_irx

audsrv.c: ../assets/audsrv.irx
	bin2c $< $@ audsrv_irx

iomanx.c: $(PS2SDK)/iop/irx/iomanX.irx
	bin2c $< $@ iomanx_irx

filexio.c: $(PS2SDK)/iop/irx/fileXio.irx
	bin2c $< $@ filexio_irx

usbd.o: usbd.c
usbhdfsd.o: usbhdfsd.c
audsrv.o: audsrv.c
iomanx.o: iomanx.c
filexio.o: filexio.c

clean:
	rm -f $(EE_OBJS) $(EE_BIN) usbd.c usbhdfsd.c iomanx.c filexio.c audsrv.c

.PHONY: all clean

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
