#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgjm/errors.h>
#include <libgjm/memory.h>
#include <libgjm/test.h>
#include <libgjm/util.h>

#include "goal.h"
#include "game.h"

static int32_t decode_value(struct game_t *game, uint32_t param) {
	if (is_flag_set(param, GOAL_ATTR_BIT)) {
		return (int32_t) game->attr_n[GOAL_VALUE(param)];
	}

	return (int32_t) sign_extend(GOAL_VALUE(param), 12);
}

static int32_t apply_op(int32_t op, int32_t a, int32_t b) {
	switch (op) {
	case GOAL_OPER_PLUS:
		return a + b;
	case GOAL_OPER_MINUS:
		return a - b;
	case GOAL_OPER_DIV:
		return a / b;
	case GOAL_OPER_MULT:
		return a * b;
	case GOAL_OPER_LT:
		return a < b;
	case GOAL_OPER_GE:
		return a >= b;
	}

	DEBUG("got a bad op: %d\n", op);
	return 0;
}

static int32_t goal_param(struct game_t *game, struct goal_t *goal) {
	int32_t compute[128] = {0};
	size_t i = 0;
	size_t j = 0;
	bool has_input = true;

	while (has_input || j > 2) {
		// Process any completed expressions on the stack
		while (j > 2
			&& is_flag_set(compute[j-3], GOAL_OPER_BIT)
			&& !is_flag_set(compute[j-2], GOAL_OPER_BIT)
			&& !is_flag_set(compute[j-1], GOAL_OPER_BIT))
		{
			uint32_t op = compute[j-3];
			int32_t a = decode_value(game, compute[j-2]);
			int32_t b = decode_value(game, compute[j-1]);
			int32_t result = apply_op(op, a, b);
			compute[j-3] = result;
			j = j - 2;
		}

		// Push more data onto the stack if available
		if (!is_flag_set(goal->params[i], GOAL_TAIL_BIT)) {
			compute[j] = goal->params[i];
			j += 1;
			i += 1;
		}
		else {
			has_input = false;
		}
	}

	return compute[0];
}

static bool check_goal(struct game_t *game, size_t i) {
	struct goal_t *goal = &game->params->goals[i];
	return goal_param(game, goal) != 0;
}

bool check_goals(struct game_t *game) {
	for (size_t i = 0; i < game->params->n_goals; ++i) {
		if (!check_goal(game, i))
			return false;
	}

	return true;
}

DEFINE_BASIC_TEST(goal_param_calc, {
	struct goal_t g1 = {
		.params = GOAL_PARAMS(
			GOAL_OPER_PLUS,
			GOAL_VALUE(4),
			GOAL_OPER_MULT,
			GOAL_VALUE(2),
			GOAL_OPER_PLUS,
			GOAL_VALUE(-10),
			GOAL_VALUE(13)
		)
	};

	TEST_EQUALS(goal_param(NULL, &g1), 10);
});
