#ifndef _GAME_H_
#define _GAME_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <uuid/uuid.h>

#include <libgjm/errors.h>
#include <libgjm/util.h>
#include <libgjm/well.h>

#include "goal.h"

// Maximum venue capacity
#define ACCEPTED_LIMIT 1000
#define LOSS_LIMIT (20000 + ACCEPTED_LIMIT)

// Maximum number of attrs permitted
#define MAX_ATTRS 7

// A person is encoded as accepted by setting a reserved bit in their attribute
// vector
#define ATTR_ACCEPT 7
#define BIT_ATTR_ACCEPT BIT(ATTR_ACCEPT)

// uuids are 36 bytes long + null terminator
#define UUID_NAME_LEN 37
#define USER_NAME_LEN 32

// Valkey storage parameters
#define VALKEY_USER_GAME_HISTORY 1000

struct game_params_t {
	struct gen_params {
		size_t n;
		double *t;
		double *a;
	} rng_params;

	// These will be computed and filled in based on the rng params
	struct {
		double *marginals;
		double *corr;
	} dist_params;

	size_t n_goals;
	struct goal_t *goals;
};

/**
 * Valkey structure for a game is
 * hash (hset, hmget) keyed by uuid (string)
 *  id -> integer
 *  userid -> integer
 *  type -> integer
 *  next -> integer
 *
 * string keyed by uuid-m
 */
struct game_t {
	char name[UUID_NAME_LEN];
	uint32_t id;
	uint32_t userid;
	int type;
	struct game_params_t *params;

	bool goals_satisfied;

	// Total number accepted
	uint32_t accepted;
	// Total number reviewed (accepted + rejected, does not include next)
	uint32_t count;
	uint32_t attr_n[MAX_ATTRS];

	// Is there a pending person
	bool has_next;
	// Attributes of pending person
	uint8_t next;

	uint8_t *seen;
};

struct user_t {
	char name[UUID_NAME_LEN];
	char realname[USER_NAME_LEN+1];
	uint32_t id;
};

error_t *init_game(void);
bool valid_game_type(size_t type);
bool game_is_finished(struct game_t *game);
void get_normals(double *a, double *b);
uint32_t generate_attributes(size_t n, double *t, double *a);
struct game_params_t *get_game_params(int type);
size_t get_number_of_games(void);

error_t *new_game(int type, struct user_t *user, struct game_t *dest);
error_t *create_next_person(struct game_t *game);
error_t *process_next_person(struct game_t *game, bool verdict);

bool find_user(uuid_t id, struct user_t *user);
error_t *find_game(uuid_t id, struct game_t *dest);
error_t *find_game_by_id(uint32_t id, struct game_t *dest);
error_t *find_game_string(const char *str, struct game_t *dest);
void release_game(struct game_t *game);

#endif
