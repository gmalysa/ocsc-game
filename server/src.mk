
# todo make deps rebuild automatically

local-ldflags = -lm -lmicrohttpd -luuid libvalkey/lib/libvalkey.a

src := goal.c game.c valkey.c

src-berghain-server-y := $(src) main.c
inc-y += ../libvalkey/include
berghain-server-ldflags-y = $(LDFLAGS_LIBGJM) $(local-ldflags)
apps-y += berghain-server

src-check-y := $(src) check.c
check-ldflags-y = $(LDFLAGS_LIBGJM) $(local-ldflags)
apps-y += check

src-server-test-y := $(src) ../$(TESTDRIVER_LIBGJM)
server-test-ldflags-y = $(local-ldflags)
apps-y += server-test
