
subdirs-y := libgjm

src-greed-y := greed.c
apps-y += greed
greed-ldflags-y = $(LDFLAGS_LIBGJM) -lm -lcurl
