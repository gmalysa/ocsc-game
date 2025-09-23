#ifndef _GOAL_H_
#define _GOAL_H_

#include <stdbool.h>
#include <stdint.h>

#include <libgjm/util.h>

// Goal parameters are a packed field with bits indicating the attr type
// and a value in the low order bits
#define GOAL_TAIL_BIT			BIT(14)
#define GOAL_OPER_BIT			BIT(13)
#define GOAL_ATTR_BIT			BIT(12)

#define GOAL_ATTR(p)			(GOAL_ATTR_BIT | GOAL_VALUE(p))
#define GOAL_VALUE(p) 			((p) & MASK(11, 0))
#define GOAL_TAIL				GOAL_TAIL_BIT

// Goal operators
#define GOAL_OPER_PLUS			(GOAL_OPER_BIT | 0)
#define GOAL_OPER_MINUS			(GOAL_OPER_BIT | 1)
#define GOAL_OPER_DIV			(GOAL_OPER_BIT | 2)
#define GOAL_OPER_MULT			(GOAL_OPER_BIT | 3)
#define GOAL_OPER_LT			(GOAL_OPER_BIT | 4)
#define GOAL_OPER_GE			(GOAL_OPER_BIT | 5)

#define GOAL_PARAMS(...) (uint32_t[]) { __VA_ARGS__, GOAL_TAIL }

struct game_t;

struct goal_t {
	uint32_t *params;
};

bool check_goals(struct game_t *game);

#endif
