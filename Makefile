EE_BIN = FCEUmm-Interface-Only-PS2.ELF
EE_OBJS = fceumm_interface.o usbd.o usbhdfsd.o iomanx.o filexio.o audsrv.o

EE_CFLAGS += -O3 -G0 -Wall -fomit-frame-pointer -funroll-loops
EE_CFLAGS += -DNEWLIB_PORT_AWARE -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D__SYS_TIME_H__
EE_CFLAGS += -include $(PS2_NEWLIB_SYS_TYPES)
EE_CFLAGS += -I$(GSKIT_INCLUDE) -I$(AUDSRV_INC) -I$(FCEU_ZLIB_INCLUDE)

EE_LDFLAGS += -L$(GSKIT_LIBDIR) -L$(DMAKIT_LIBDIR) -L$(PS2SDK)/ports/lib -L$(AUDSRV_LIBDIR)
EE_LIBS += -laudsrv -lgskit -ldmakit -lpad -lfileXio -lpatches $(FCEU_ZLIB_LIB) -lm -lstdc++

all: $(EE_BIN)

clean:
	rm -f $(EE_OBJS) $(EE_BIN) usbd.c usbhdfsd.c iomanx.c filexio.c audsrv.c

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
