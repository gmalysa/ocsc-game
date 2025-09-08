CFG_TESTAPP := greed-test

CC := clang

DEBUG := -ggdb -O0
#LTO := -flto

CFLAGS += -Wall -Wextra -O2 $(CFLAGS_LIBGJM) $(LTO) $(DEBUG)
LDFLAGS += -pthread $(LTO)

include libgjm/Makefile.base
