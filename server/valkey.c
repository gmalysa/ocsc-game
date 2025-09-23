#include <sched.h>
#include <libgjm/memory.h>

#include "valkey.h"

DEFINE_ERROR_MESSAGE(ERROR_ID_VALKEY, "valkey error");

static struct valkey_t *vk_list = NULL;

/**
 * Unlocked single threaded pool initializer
 */
error_t *init_valkey(void) {
	for (size_t i = 0; i < VALKEY_POOL_SIZE; ++i) {
		struct valkey_t *vk = calloc(1, sizeof(*vk));

		vk->ctx = valkeyConnectUnix(VALKEY_SOCKET_PATH);
		if (!vk->ctx || vk->ctx->err) {
			ERROR("failed to connect to valkey: %s\n",
				vk->ctx ? vk->ctx->errstr : "out of memory");
			exit(1);
		}

		vk->next = vk_list;
		vk_list = vk;
	}
	return OK;
}

/**
 * No bounded waits or fair scheduling, rewrite with pthread primitives instead.
 * Originally this was just going to be a basic lock free version but with valkey
 * accesses happening another thread could take a while so sched_yield was added
 * to allow the actual thread in question to make progress, but if we're out of
 * connections in the pool and several new request threads pile up one of them could
 * just get starved arbitrarily long
 */
struct valkey_t *get_valkey(void) {
	struct valkey_t *ret;

	do {
		ret = NULL;
		while (!ret) {
			ret = READ_RELAXED(&vk_list);
			if (!ret)
				sched_yield();
		}
	} while (!CAS_RELAXED(&vk_list, ret, vk_list->next));

	return ret;
}

void release_valkey(struct valkey_t *vk) {
	do {
		vk->next = READ_RELAXED(&vk_list);
	} while (!CAS_RELAXED(&vk_list, vk->next, vk));
}

