#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include <libgjm/errors.h>
#include <libgjm/debug.h>
#include <libgjm/binary_map.h>

#define MAX_ATTR 10
#define MAX_GOALS 10

static char *userid = "f9876829-7686-4dc3-91e6-f62a3dac9031";
static char gameid[40];
static uint32_t personid = 0;
static bool first = true;

// Table of probability values here, in order of attributes as set up
// in the table below

// Game 0
static float __p[] = {
	0.361586f, 0.411255f
};
// Scenario 2
//static float __p[] = {
//	0.6265f, 0.47f, 0.06227f, 0.398f,
//};
// Scenario 3
//static float __p[] = {
//	0.6795f, 0.5735f, 0.691f, 0.04614f, 0.04454f, 0.4564f
//};

// Correlation matrix, with indices matching the order set up in the table below
// Game 0
static float __r[2][2] = {
	{1.0f, 0.781504f},
	{0.781504f, 1.0f},
};
// Scenario 2
//static float __r[4][4] = {
//	{1.0f, -0.469616933267432f, 0.0946331703989159f, -0.654940381560618f},
//	{-0.469616933267432f, 1.0f, 0.141972591404715f, 0.572406780843645f},
//	{0.0946331703989159f, 0.141972591404715f, 1.0f, 0.144464595056508f},
//	{-0.654940381560618f, 0.572406780843645f, 0.144464595056508f, 1.0f},
//};
// Scenario 3
//static float __r[6][6] = {
//	{1.0f, -0.0811017577715299f, -0.169656347550531f, 0.0371992837675389f,
//		0.0722352115638984f, 0.111887667034228f},
//	{-0.0811017577715299f, 1.0f, 0.375711059360155f, 0.00366933143887117f,
//		-0.0308324709818108f, -0.71725293825194f},
//	{-0.169656347550531f, 0.375711059360155f, 1.0f, -0.00345309267933775f,
//		-0.110247196063585f, -0.35210244615974f},
//	{0.0371992837675389f, 0.00366933143887117f, -0.00345309267933775f, 1.0f,
//		0.479906408031673f, 0.047973811326805f},
//	{0.0722352115638984f, -0.0308324709818108f, -0.110247196063585f,
//		0.479906408031673f, 1.0f, 0.099844522862699f},
//	{0.111887667034228f, -0.71725293825194f, -0.35210244615974f,
//		0.047973811326805f, 0.099844522862699f, 1.0f},
//};

struct person {
	uint64_t attr[MAX_ATTR];
	size_t n;
};

struct goal {
	uint64_t attr;
	int64_t num;
	// min length, used as scratch storage
	int64_t L;
};

struct goals {
	// potentially sorted list of goals
	struct goal *g[MAX_GOALS];
	size_t n;

	int64_t space;

	// Backing array, don't use this in algo methods
	struct goal _goals[MAX_GOALS];
};

static struct goals *all_goals;
static CURL *curl;

void dump_exit(void) {
	ERROR("exit. goal state:\n");
	for (size_t i = 0; i < all_goals->n; ++i) {
		ERROR("goal %zd: attr %zd, remain %zd\n", i, all_goals->g[i]->attr,
			all_goals->g[i]->num);
	}
	ERROR("total space: %zd\n", all_goals->space);
	ERROR("game uuid: %s\n", gameid);
	ERROR("current personid: %u\n", personid);

	if (curl)
		curl_easy_cleanup(curl);
	exit(1);
}

struct goals *alloc_goals(size_t n) {
	struct goals *res = calloc(1, sizeof(*res));
	res->n = n;

	for (size_t i = 0; i < n; ++i) {
		res->g[i] = &res->_goals[i];
	}

	return res;
}

void free_goals(struct goals *g) {
	free(g);
}

/**
 * Clone all but the first goal into a new list
 */
struct goals *clone_rest(struct goals *goals) {
	struct goals *res;

	res = alloc_goals(goals->n - 1);

	for (size_t i = 1; i < goals->n; ++i) {
		memcpy(res->g[i-1], goals->g[i], sizeof(struct goal));
	}

	return res;
}

struct binary_map attrmap;

void init_attrmap(void) {
	static uint64_t __attr[MAX_ATTR] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	};

	// Up to 10 attributes supported currently, set the key to attribute id
	// for everything in the response to new-game and pull terms sequentially
	// from __attr
	struct binary_map_entry entries[] = {
		{.key = 0, .value = &__attr[0]},
		{.key = 1, .value = &__attr[1]},
//		{.key = 2, .value = &__attr[2]},
//		{.key = 3, .value = &__attr[3]},
//		{.key = 4, .value = &__attr[4]},
//		{.key = 5, .value = &__attr[5]},
	};

	error_t *ret;

	ret = binary_map_populate(&attrmap, entries, ARRAY_SIZE(entries));
	if (NOT_OK(ret)) {
		error_print(ret);
		ERROR("unable to init binary map of attributes, quitting");
		dump_exit();
	}
}

float get_p(uint64_t attr) {
	uint64_t *ap = binary_map_lookup(&attrmap, attr);

	if (!ap) {
		ERROR("invalid attr %zu", attr);
		dump_exit();
	}

	return __p[*ap];
}

float get_correlation(uint64_t a, uint64_t b) {
	uint64_t *ap = binary_map_lookup(&attrmap, a);
	uint64_t *bp = binary_map_lookup(&attrmap, b);

	if (!ap || !bp) {
		ERROR("invalid attr %zu or %zu", a, b);
		dump_exit();
	}

	return __r[*ap][*bp];
}

/**
 * This returns p(a = 1 | given = 1) using the correlation-based linear
 * interpolation
 */
float get_p_given(uint64_t a, uint64_t given) {
	float pa = get_p(a);
	float r = get_correlation(a, given);

	if (r < 0)
		return pa * (1 + r);
	else
		return pa + r*(1 - pa);
}

/**
 * Adjust goals 1-n based on how much progress the first goal might make, in a
 * new, smaller goal array
 */
struct goals *adjust_goal_rest(struct goals *goals) {
	struct goals *rest = clone_rest(goals);
	rest->space = goals->space - goals->g[0]->num;
	DEBUG("adjust space by %zd from %zd -> %zd\n",
		goals->g[0]->num, goals->space, rest->space);

	for (size_t i = 0; i < rest->n; ++i) {
		float condp = get_p_given(rest->g[i]->attr, goals->g[0]->attr);
		int64_t adj = ceilf(goals->g[0]->num * condp);
		rest->g[i]->num = rest->g[i]->num - adj;
		DEBUG("adjust %zd by %zd from %zd -> %zd\n",
			rest->g[i]->attr, adj, goals->g[i+1]->num, rest->g[i]->num);
	}

	return rest;
}

/**
 * Get the minimum length expected for a given goal
 */
int64_t getL(struct goal *g) {
	return (int64_t) ceilf(g->num / get_p(g->attr));
}

int goal_lencomp(const void *_a, const void *_b) {
	const struct goal * const *a = _a;
	const struct goal * const *b = _b;
	return (int) ((*b)->L - (*a)->L);
}

void sort_by_L(struct goals *goals) {
	size_t i;

	for (i = 0; i < goals->n; ++i) {
		goals->g[i]->L = getL(goals->g[i]);
	}

	qsort(goals->g, goals->n, sizeof(*goals->g), goal_lencomp);

	DEBUG("sorted goals: ");
	for (i = 0; i < goals->n; ++i) {
		DEBUG("[%zu]=a:%zd, n:%zd, L:%zd ", i, goals->g[i]->attr, goals->g[i]->num,
			goals->g[i]->L);
	}
	DEBUG("\n");
}

/**
 * If we need as many of this attribute as we have space left, then this attribute
 * must be set on everyone we accept from here on out
 */
bool is_attr_required(uint64_t attr, struct goals *goals) {
	for (size_t i = 0; i < goals->n; ++i) {
		if (goals->g[i]->attr == attr) {
			if (goals->g[i]->num == goals->space)
				return true;
		}
	}
	return false;
}

bool person_has_attr(struct person *p, uint64_t attr) {
	for (size_t i = 0; i < p->n; ++i) {
		if (p->attr[i] == attr)
			return true;
	}
	return false;
}

/**
 * Return true if this person should be rejected because they're missing attributes
 * we need to complete the requirements
 */
bool reject_for_required(struct person *p, struct goals *goals) {
	size_t i;

	// adjusted space issue?
	if (goals->space <= 0) {
		DEBUG("got space <= 0 while checking for required attributes\n");
		return false;
	}

	for (i = 0; i < goals->n; ++i) {
		// if this goal is required
		if (goals->g[i]->num >= goals->space) {
			// and this person does not have this attribute
			if (!person_has_attr(p, goals->g[i]->attr)) {
				DEBUG("p is missing required attr %zd (needs %zd of %zd)\n",
					goals->g[i]->attr, goals->g[i]->num, goals->space);
				return true;
			}
		}
	}

	// either they have all attributes or none are required
	return false;
}

/**
 * decide what to do for the given person
 */
bool decide_for(struct person *p, struct goals *goals, bool guess) {
	struct goals *rest;
	bool ret;

	// If we are out of goals then we can accept anyone left so long
	// as there is space for them
	if (goals->n == 0) {
		DEBUG("out of constraints, accepting if we have room\n");
		return goals->space > 0;
	}

	// No losers
//	if (p->n == 0) {
//		DEBUG("rejected as a loser\n");
//		return false;
//	}

	// First check if this person's acceptance would cause us to fail
	if (reject_for_required(p, goals)) {
		DEBUG("rejecting due to missing required attr\n");
		return false;
	}

	// now arrange our goals in order of most difficult to easiest and figure out
	// if they would help us
	sort_by_L(goals);

	DEBUG("target attr %zd needs %zd of %zd, L = %zd\n", goals->g[0]->attr,
		goals->g[0]->num, goals->space, goals->g[0]->L);

	// If the hardest remaining goal has no requirements this person is
	// also a loser
	if (goals->g[0]->num <= 0) {
		DEBUG("out of constraints, reclassifying as a loser\n");
//		return false;
		return goals->space > 0;
	}

	// They match our hardest goal
	if (person_has_attr(p, goals->g[0]->attr)) {
		// When we're guessing, we should still reject some fraction
		// of these partial matches in order to reduce pressure on the hardest
		// goal
//		if (guess) {
//		}
//		else {
			DEBUG("matches attr and we have room, accepting\n");
			return true;
//		}
	}

	// They do not match but if we have room to expect them based on the expected
	// reduction from the first goal, go ahead and accept them
	INDENT(1);
	rest = adjust_goal_rest(goals);
	ret = decide_for(p, rest, true);
	UNDENT(1);
	free_goals(rest);
	return ret;
}

/**
 * This person has been accepted, decrement goals and space
 */
void update_goals(struct person *p, struct goals *goals) {
	goals->space -= 1;

	for (size_t i = 0; i < goals->n; ++i) {
		if (person_has_attr(p, goals->g[i]->attr))
			goals->g[i]->num -= 1;
	}

	// If we have completed goals at the end drop them now from what we will consider
	// if there are any earlier, the next sort will move them to the end of the list
	while (goals->n > 0 && goals->g[goals->n-1]->num <= 0) {
		DEBUG("finished goal for attr %zd and dropped it\n", goals->g[goals->n-1]->attr);
		goals->n -= 1;
	}

	DEBUG("space remaining: %zd\n", goals->space);
	for (size_t i = 0; i < goals->n; ++i) {
		DEBUG("goal %zu: attr %zu requires %zd more\n",
			i, goals->g[i]->attr, goals->g[i]->num);
	}
}

static char body[65535];

/**
 * Only one request at a time so save the response into a single buffer
 */
size_t save_body(char *ptr, size_t size, size_t nmemb, void *data) {
	UNUSED(data);

	if (size*nmemb + 1 > sizeof(body)) {
		DEBUG("got a body too big? %zu, %zu -> %zu\n", size, nmemb, size*nmemb+1);
		dump_exit();
	}
	memcpy(body, ptr, nmemb*size);
	body[nmemb*size] = '\0';
	return nmemb*size;
}

bool parse_person(struct person *p, bool first) {
	char *s;
	uint32_t current;

	ERROR("current body: %s\n", body);

	s = strstr(body, "\"error\"");
	if (s) {
		DEBUG("received an error reply: %s\n", body);
		dump_exit();
	}

	s = strstr(body, "\"status\"");
	s = s + 9;

	while (*s && *s != '"')
		s++;

	if (strncmp(s+1, "failed", 6) == 0 || strncmp(s+1, "completed", 9) == 0) {
		DEBUG("status for being done\n%s", body);
		return false;
	}

	// Double check that we agree on how many were admitted
	s = strstr(s, "\"count\"");
	while (!isdigit(*s))
		s++;

	current = atoi(s);
	if (!first) {
		if (current != personid + 1) {
			ERROR("expected count %u, got %u\n", personid, current);
			dump_exit();
		}
	}
	else if (current != 0) {
		ERROR("expected first person's count of 0 got %u\n", current);
		dump_exit();
	}

	personid = current;

	// Read attributes for next patron
	s = strstr(s, "\"next\"");
	while (!isdigit(*s))
		s++;

	p->n = 0;

	current = atoi(s);
	for (size_t i = 0; i < MAX_ATTR; ++i) {
		if (is_flag_set(current, BIT(i))) {
			p->attr[p->n] = i;
			p->n += 1;
		}
	}

	DEBUG("new person received with %zu attributes:", p->n);
	for (size_t i = 0; i < p->n; ++i) {
		DEBUG(" %zu", p->attr[i]);
	}
	DEBUG("\n");

	return true;
}

void new_game(void) {
	char urlbuf[256];
	char *s;
	char *e;
	CURLcode res;

	snprintf(urlbuf, sizeof(urlbuf),
		"http://localhost:8124/game/new-game?user=%s&type=0",
		userid);

	curl_easy_setopt(curl, CURLOPT_URL, urlbuf);
	res = curl_easy_perform(curl);

	if (res != CURLE_OK) {
		ERROR("failed to start new game CURLcode = %d\n", res);
		dump_exit();
	}

	s = strstr(body, "\"id\"");
	s = strstr(s, ":");
	s = strstr(s, "\"") + 1;
	e = strstr(s, "\"");
	strncpy(gameid, s, e-s);
	DEBUG("new game uuid: %s\n", gameid);
}

bool get_person(struct person *p, bool action, bool first) {
	char urlbuf[256];
	CURLcode res;

	if (first) {
		snprintf(urlbuf, sizeof(urlbuf),
		"http://localhost:8124/game/process-person?game=%s&person=%d",
		gameid, personid);
	}
	else {
		snprintf(urlbuf, sizeof(urlbuf),
		"http://localhost:8124/game/process-person?game=%s&person=%d&verdict=%s",
			gameid, personid, action ? "true" : "false");
	}

	DEBUG("making request for %s\n", urlbuf);

	curl_easy_setopt(curl, CURLOPT_URL, urlbuf);
	res = curl_easy_perform(curl);

	if (res != CURLE_OK) {
		ERROR("failure from curl = %d\n", res);
		dump_exit();
	}

	return parse_person(p, first);
}

int main(int argc, char **argv) {
	bool choice;
	struct person p;
	struct goals *goals = alloc_goals(2);
	goals->space = 1000;

	UNUSED(argc);
	UNUSED(argv);

	// Scenario 1
	goals->g[0]->attr = 0;
	goals->g[0]->num = 600;
	goals->g[1]->attr = 1;
	goals->g[1]->num = 600;

	all_goals = goals;
	init_attrmap();

	sort_by_L(goals);

	curl = curl_easy_init();
	if (!curl) {
		ERROR("curl easy init failed\n");
		dump_exit();
	}

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, save_body);

	new_game();

	choice = false;
	while (true) {
		if (!get_person(&p, choice, first)) {
			dump_exit();
		}

		first = false;
		choice = decide_for(&p, goals, false);
		if (choice)
			update_goals(&p, goals);
	}

	return 0;
}
