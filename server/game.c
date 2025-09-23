#include <math.h>
#include <pthread.h>

#include "goal.h"
#include "game.h"
#include "valkey.h"

// Number of warm up loops to use with WELL before running games off it
#define RNG_INIT_LOOPS 1000

static pthread_spinlock_t rng_lock;
static struct well_state_t rng = {0};

static struct game_params_t game_params0 = {
	.rng_params = {
		.n = 2,
		.t = (double[]) {0.5, 0.5},
		.a = (double[]) {1.0, 1.0,
		                 1.0, 2.0},
	},
	.dist_params = {
		.marginals = (double[2]) {0},
		.corr = (double[4]) {0},
	},
	.goals = (struct goal_t[]) {
		{
			.params = GOAL_PARAMS(
						GOAL_OPER_GE,
						GOAL_ATTR(0),
						GOAL_VALUE(600))
		}, {
			.params = GOAL_PARAMS(
						GOAL_OPER_GE,
						GOAL_ATTR(1),
						GOAL_VALUE(600))
		}
	},
	.n_goals = 2,
};

static struct game_params_t game_params1 = {
	.rng_params = {
		.n = 4,
		.t = (double[]) {0.75, 0.2, 0.4, 0.7},
		.a = (double[]) {1.0, 0.0, 0.0,  0.0,
		                 0.0, 1.0, 2.0, -2.0,
						 0.0, 0.0, 1.0, -1.0,
						 0.0, 0.0, 0.0,  1.0},
	},
	.dist_params = {
		.marginals = (double[4]) {0},
		.corr = (double[16]) {0},
	},
	.goals = (struct goal_t[]) {
		{
			.params = GOAL_PARAMS(
						GOAL_OPER_GE,
						GOAL_ATTR(1),
						GOAL_OPER_DIV,
						GOAL_ATTR(0),
						GOAL_VALUE(2))
		}, {
			.params = GOAL_PARAMS(
						GOAL_OPER_GE,
						GOAL_ATTR(2),
						GOAL_OPER_DIV,
						GOAL_ATTR(3),
						GOAL_VALUE(2))

		}
	},
	.n_goals = 2,
};

/**
 * @todo this needs th closed form expression for the correlation
 */
static void assign_dist_params(struct game_params_t *params) {
	size_t i, j;
	size_t n = params->rng_params.n;

 /*
  * The marginal probability that an attirbute is set is:
  * p(X = 1) = Pr(sum(a_i*x_i) > t) = 1 - Pr(sum(...) <= t)
  * A linear combination of normally distributed random variables is normally
  * distributed with variance = sum(a_i^2 * var(x_i)), so Pr(sum(...) <= t) can be
  * found in the marginal case as: F(t / sqrt(sum(a_i^2))). Or in terms of C primitives,
  * p(X = 1) = 0.5 - 0.5*erf(t / sqrt(2*sum(a_i^2)))
  */
	for (i = 0; i < n; ++i) {
		double sum = 0;
		for (j = 0; j < n; ++j) {
			sum += pow(params->rng_params.a[i*n + j], 2);
		}

		params->dist_params.marginals[i] =
			0.5*(1 - erf(params->rng_params.t[i] / sqrt(2*sum)));
	}
}

/**
 * Passable state initialization for WELL. It is claimed that it achieves a good
 * distribution from an arbitrary state initialization within a reasonably small
 * number of samples, so we prepopulate with garbage from rand and iterate some
 * time to guess that we're in the well behaved region
 *
 * Unlocked call because this should be done at init time before other threads
 * are activated
 */
static void init_rng(void) {
	size_t i;

	memset(&rng, 0, sizeof(rng));
	srand(time(NULL));

	for (i = 0; i < 32; ++i) {
		rng.state[i] = rand();
	}

	for (i = 0; i < RNG_INIT_LOOPS; ++i) {
		UNUSED(well_1024a(&rng));
	}

	pthread_spin_init(&rng_lock, 0);
}

error_t *init_game(void) {
	init_rng();

	assign_dist_params(&game_params0);
	assign_dist_params(&game_params1);

	return init_valkey();
}

/**
 * Using the box-muller transform obtain two normals at once, with 4 calls to well
 */
void get_normals(double *a, double *b) {
	size_t i;
	uint32_t vals[4];
	uint64_t u0i, u1i;
	double u0, u1;

	pthread_spin_lock(&rng_lock);

	for (i = 0; i < 4; ++i) {
		vals[i] = well_1024a(&rng);
	}

	pthread_spin_unlock(&rng_lock);

	u0i = (((uint64_t) vals[0]) << 32) | vals[1];
	u1i = (((uint64_t) vals[2]) << 32) | vals[3];

	u0 = 2.0 * M_PI * u0i / 0x1p64;
	u1 = sqrt(-2.0 * log(u1i / 0x1p64));
	*a = u1 * cos(u0);
	*b = u1 * sin(u0);
}

/**
 * Generate a random set of attributes according to the parameters given:
 * - t_i is the threshold for the resulting normal
 * - a_i,j is the jth coefficient for combining the required normals
 *
 * In this case, a[0]..a[n-1] is for the first one, a[n]-a[2n-1] is for the
 * second one and so forth. You could describe it as a matrix multiplication
 * if you wanted to.
 *
 * Finding the correlation between two attributes is more complicated
 *
 * This can be used for up to 32 attributes encoded as a bitfield, and n must
 * be a multiple of 2
 */
uint32_t generate_attributes(size_t n, double *t, double *a) {
	size_t i, j;
	size_t base;
	double x[n];
	double sums[n];
	uint32_t res = 0;

	if (n == 0 || (n & 1)) {
		ERROR("Invalid value for n in generate_attributes: %zu\n", n);
		return 0;
	}

	for (i = 0; i < n/2; ++i) {
		get_normals(&x[2*i], &x[2*i+1]);
	}

	for (i = 0; i < n; ++i) {
		base = i*n;
		sums[i] = 0.0;
		for (j = 0; j < n; ++j) {
			sums[i] += x[j] * a[base + j];
		}
	}

	for (i = 0; i < n; ++i) {
		if (sums[i] > t[i])
			res |= BIT(i);
	}

	return res;
}

bool valid_game_type(int type) {
	switch (type) {
	case 0:
		return true;
	}

	return false;
}

struct game_params_t *get_game_params(int type) {
	switch (type) {
	case 0:
		return &game_params0;
	case 1:
		return &game_params1;
//	case 2:
//		return &game_params2;
	default:
		return NULL;
	}
}

bool game_is_finished(struct game_t *game) {
	return (game->accepted >= ACCEPTED_LIMIT) || (game->count >= LOSS_LIMIT);
}

void game_update(struct game_t *game) {
	game->accepted = 0;
	for (size_t i = 0; i < MAX_ATTRS; ++i)
		game->attr_n[i] = 0;

	for (uint32_t i = 0; i < game->count; ++i) {
		if (is_flag_set(game->seen[i], BIT_ATTR_ACCEPT))
			game->accepted += 1;

		for (uint8_t attr = 0; attr < MAX_ATTRS; ++attr) {
			if (is_flag_set(game->seen[i], BIT(attr))) {
				game->attr_n[attr] += 1;
			}
		}
	}

	if (game_is_finished(game))
		game->goals_satisfied = check_goals(game);
}

error_t *create_next_person(struct game_t *game) {
	uint32_t attr;
	valkeyReply *reply;
	error_t *ret = OK;
	struct valkey_t *vk = get_valkey();

 	attr = generate_attributes(game->params->rng_params.n, game->params->rng_params.t,
		game->params->rng_params.a);
	game->next = (uint8_t) attr;

	reply = valkeyCommand(vk->ctx, "HMSET %s next %d", game->name, attr);
	if (!reply || reply->type == VALKEY_REPLY_ERROR)
		ret = E_VALKEY(vk->ctx, reply);

	freeReplyObject(reply);
	release_valkey(vk);
	return ret;
}

error_t *new_game(int type, struct user_t *user, struct game_t *dest) {
	uuid_t uuid;
	struct valkey_t *vk;
	valkeyReply *reply;
	char localbuf[128];

	memset(dest, 0, sizeof(*dest));

	vk = get_valkey();
	reply = valkeyCommand(vk->ctx, "INCR next_game");
	if (!reply || reply->type == VALKEY_REPLY_ERROR)
		goto fail_reply;

	uuid_generate(uuid);
	uuid_unparse(uuid, dest->name);
	dest->id = reply->integer;
	dest->userid = user->id;
	dest->type = type;
	dest->params = get_game_params(type);

	freeReplyObject(reply);
	reply = valkeyCommand(vk->ctx, "HSET %s id %d userid %d type %d", dest->name,
		dest->id, user->id, type);
	if (!reply || reply->type == VALKEY_REPLY_ERROR)
		goto fail_reply;

	freeReplyObject(reply);
	reply = valkeyCommand(vk->ctx, "HSET gameids %d %s", dest->id, dest->name);
	if (!reply || reply->type == VALKEY_REPLY_ERROR)
		goto fail_reply;

	snprintf(localbuf, sizeof(localbuf), "%s-games", user->name);

	freeReplyObject(reply);
	reply = valkeyCommand(vk->ctx, "LPUSH %s %s\n", localbuf, dest->name);
	if (!reply || reply->type == VALKEY_REPLY_ERROR) {
		DEBUG("orphaned game %s, owned by user %s\n", dest->name, user->name);
		goto fail_reply;
	}

	freeReplyObject(reply);
	release_valkey(vk);
	return create_next_person(dest);

fail_reply:
	freeReplyObject(reply);
	release_valkey(vk);
	return E_MSG("valkey failure");
}

bool find_user(uuid_t id, struct user_t *user) {
	struct valkey_t *vk;
	struct valkeyReply *reply;

	uuid_unparse_lower(id, user->name);

	vk = get_valkey();
	reply = valkeyCommand(vk->ctx, "HMGET %s id name", user->name);
	if (!reply || reply->type == VALKEY_REPLY_ERROR) {
		release_valkey(vk);
		return false;
	}

	if (reply->elements != 2) {
		release_valkey(vk);
		return false;
	}

	user->id = reply->element[0]->integer;
	memset(user->realname, 0, sizeof(user->realname));
	ASSERT(reply->element[1]->len <= USER_NAME_LEN);
	memcpy(user->realname, reply->element[1]->str, reply->element[1]->len);

	freeReplyObject(reply);
	release_valkey(vk);
	return true;
}

error_t *find_game(uuid_t id, struct game_t *dest) {
	char keybuf[UUID_NAME_LEN+2]; // for -m
	struct valkey_t *vk;
	struct valkeyReply *reply;
	error_t *ret;

	uuid_unparse_lower(id, dest->name);

	vk = get_valkey();

	reply = valkeyCommand(vk->ctx, "HGETALL %s", dest->name);
	if (!reply || reply->type == VALKEY_REPLY_ERROR)
		goto fail_valkey;

	// List of 2*n elements of key then value
	for (size_t i = 0; i < reply->elements; i += 2) {
		struct valkeyReply *key = reply->element[i];
		struct valkeyReply *val = reply->element[i + 1];

		if (STRING_EQUALS(key->str, "id")) {
			dest->id = atoi(val->str);
		}
		else if (STRING_EQUALS(key->str, "userid")) {
			dest->userid = atoi(val->str);
		}
		else if (STRING_EQUALS(key->str, "type")) {
			int type = atoi(val->str);
			dest->params = get_game_params(type);
			dest->type = type;
		}
		else if (STRING_EQUALS(key->str, "next")) {
			dest->next = (uint8_t) atoi(val->str);
			dest->has_next = true;
		}
	}

	snprintf(keybuf, sizeof(keybuf), "%s-m", dest->name);
	freeReplyObject(reply);
	reply = valkeyCommand(vk->ctx, "GET %s", keybuf);
	if (!reply || reply->type == VALKEY_REPLY_ERROR)
		goto fail_valkey;

	// Add 1 to length for a potential next person
	dest->seen = calloc(reply->len+1, 1);
	if (!dest->seen) {
		ret = E_NOMEM;
		goto fail_err;
	}

	memcpy(dest->seen, reply->str, reply->len);
	dest->count = (uint32_t) reply->len;
	game_update(dest);
	freeReplyObject(reply);
	release_valkey(vk);
	return OK;

fail_valkey:
	ret = E_VALKEY(vk->ctx, reply);
fail_err:
	freeReplyObject(reply);
	release_valkey(vk);
	return ret;
}

error_t *find_game_by_id(uint32_t id, struct game_t *dest) {
	uuid_t uuid;
	struct valkey_t *vk;
	valkeyReply *reply;
	error_t *ret = OK;

	vk = get_valkey();
	reply = valkeyCommand(vk->ctx, "HGET gameids %d", id);
	if (!reply || reply->type == VALKEY_REPLY_ERROR) {
		ret = E_VALKEY(vk->ctx, reply);
		goto done;
	}

	if (reply->type != VALKEY_REPLY_STRING) {
		ret = E_MSG("invalid game id");
		goto done;
	}

	if (uuid_parse(reply->str, uuid) < 0) {
		ret = E_MSG("invalid uuid");
		goto done;
	}

	freeReplyObject(reply);
	release_valkey(vk);
	return find_game(uuid, dest);

done:
	freeReplyObject(reply);
	release_valkey(vk);
	return ret;
}

void release_game(struct game_t *game) {
	if (game->seen)
		free(game->seen);
}

error_t *process_next_person(struct game_t *game, bool verdict) {
	struct valkey_t *vk;
	uint8_t attr;
	uint32_t offset;
	char keybuf[40];
	valkeyReply *reply;
	error_t *ret;

	if (game->accepted >= ACCEPTED_LIMIT || game->count >= LOSS_LIMIT)
		return E_MSG("game finished");

	if (!game->has_next)
		return E_MSG("no patron available");

	snprintf(keybuf, sizeof(keybuf), "%s-m", game->name);

	attr = game->next;
	offset = game->count;
	if (verdict)
		attr |= BIT_ATTR_ACCEPT;

	vk = get_valkey();

	reply = valkeyCommand(vk->ctx, "HDEL %s next", game->name);
	if (!reply || (reply->type == VALKEY_REPLY_ERROR))
		goto failure;

	freeReplyObject(reply);
	reply = valkeyCommand(vk->ctx, "SETRANGE %s %d %b", keybuf, offset, &attr,
		sizeof(attr));
	if (!reply || (reply->type == VALKEY_REPLY_ERROR))
		goto failure;

	game->seen[game->count] = attr;
	game->count += 1;
	game_update(game);

	freeReplyObject(reply);
	release_valkey(vk);
	return OK;

failure:
	ret = E_VALKEY(vk->ctx, reply);
	freeReplyObject(reply);
	release_valkey(vk);
	return ret;
}

