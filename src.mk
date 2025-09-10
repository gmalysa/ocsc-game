
subdirs-y := libgjm

src-greed-y := greed.c
apps-y += greed
greed-ldflags-y = $(LDFLAGS_LIBGJM) -lm -lcurl

src-analyze-y := analyze.c
apps-y += analyze
analyze-ldflags-y = $(LDFLAGS_LIBGJM)
