#ifndef _VALKEY_H_
#define _VALKEY_H_

#include <valkey/valkey.h>

#include <libgjm/binary_map.h>
#include <libgjm/debug.h>
#include <libgjm/errors.h>

// valkey pool affects how many concurrent connections we can handle
#define VALKEY_POOL_SIZE 16
#define VALKEY_SOCKET_PATH "/tmp/berghain.sock"

// @todo actually should be a cfg to play nice with other modules but for now
// this error id is available in this project
#define ERROR_ID_VALKEY ERROR_USER_ID(0)

// __error is designed for statically allocated strings, but reply and ctx can both
// be deallocated so we dump the error to DEBUG and return a generic message in
// __error for now, until it gains the ability to have string ownership
#define E_VALKEY(ctx, reply) \
	({ DEBUG("valkey error: %s\n", reply ? reply->str  : ctx->errstr); \
		__error(ERROR_ID_VALKEY, NULL, ERROR_ARG(0, 0, 0, NULL)); })

struct valkey_t {
	valkeyContext *ctx;
	struct valkey_t *next;
};

error_t *init_valkey(void);
struct valkey_t *get_valkey(void);
void release_valkey(struct valkey_t *vk);

#endif
