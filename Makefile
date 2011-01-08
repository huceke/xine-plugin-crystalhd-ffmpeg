XINEPLUGIN = xineplug_decode_crystalhd.so 

INSTALL       = install
XINEPLUGINDIR  = $(shell pkg-config --variable=plugindir libxine)

#CFLAGS        = -g -O2 -march=core2 -msse2 -mssse3 -mfpmath=sse -fomit-frame-pointer -pipe -DNOVDPAU -fPIC -Wall
CFLAGS        = -g -O2 -march=nocona -pipe -DNOVDPAU -fPIC -Wall
CFLAGS        += $(shell pkg-config --cflags libxine) -I/usr/include/libcrystalhd
LIBS          += $(shell pkg-config --libs libxine) -lcrystalhd
#LDFLAGS       = -shared -fvisibility=hidden -fPIC
LDFLAGS       = -shared -fvisibility=hidden -g -fPIC -lavcodec -lavutil -lavformat

CFLAGS += -DEXPORTED=__attribute__\(\(visibility\(\"default\"\)\)\)

OBJ = crystalhd_converter.o crystalhd_hw.o crystalhd_decoder.o 

all: clean $(XINEPLUGIN)

$(XINEPLUGIN): $(OBJ)
	$(CC) $(LDFLAGS) $(LIBS) $(OBJ) -o $@

.c: %.o
		$(CC) $(CFLAGS) $< -o $@

install: all
	@echo Installing $(XINEPLUGINDIR)/$(XINEPLUGIN)
	@-rm -rf $(XINEPLUGINDIR)/*crystalhd*
	@$(INSTALL) -D -m 0755 $(XINEPLUGIN) $(XINEPLUGINDIR)/$(XINEPLUGIN)

clean:
	@-rm -f $(XINEPLUGIN) *.o

.PHONY: $(XINEPLUGIN) 
