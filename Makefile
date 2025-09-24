CFG_TESTAPP := greed-test

CC := gcc

DEBUG := -ggdb -O2
#LTO := -flto

CFLAGS += -Wall -Wextra -O2 $(CFLAGS_LIBGJM) $(LTO) $(DEBUG)
LDFLAGS += -pthread $(LTO)

include libgjm/Makefile.base
