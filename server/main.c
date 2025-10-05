#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <microhttpd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <valkey/valkey.h>

#include <libgjm/debug.h>
#include <libgjm/errors.h>
#include <libgjm/test.h>
#include <libgjm/util.h>

#include "goal.h"
#include "game.h"
#include "valkey.h"

#define GAME_PORT 8124

struct MHD_Response *web_reply_json(char *msg) {
	struct MHD_Response *reply;
	reply = MHD_create_response_from_buffer(strlen(msg), msg, MHD_RESPMEM_MUST_COPY);
	MHD_add_response_header(reply, "Content-Type", "application/json");
	return reply;
}

enum MHD_Result web_bad_arg(struct MHD_Connection *conn, const char *name) {
	char msg[128];

	snprintf(msg, sizeof(msg), "{\"error\":\"bad or missing arg %s\"}", name);
	DEBUG("request with bad or missing arg %s\n", name);
	return MHD_queue_response(conn, MHD_HTTP_OK, web_reply_json(msg));
}

enum MHD_Result web_set_cookie(struct MHD_Response *resp, const char *cookie,
	const char *value)
{
	// Expire a year from now surely nobody will be playing that long
	time_t exptime = time(NULL) + (3600*24*365);
	struct tm *gmt = gmtime(&exptime);
	char expires[64];
	char msg[128];

	strftime(expires, sizeof(expires), "%a, %d %b %Y %H:%M:%S GMT", gmt);
	snprintf(msg, sizeof(msg), "%s=%s; Expires=%s; Path=/",
		cookie, value, expires);
	return MHD_add_response_header(resp, "Set-Cookie", msg);
}

enum MHD_Result web_send_error(struct MHD_Connection *conn, error_t * __mine err) {
	struct ioport *iop;
	char msg[128];

	iop = iop_alloc_fixstr(msg, sizeof(msg));
	iop_printf(iop, "{\"error\":\"");
	error_ioprint(err, iop);
	iop_printf(iop, "\"}");

	DEBUG("request with inline error object:");
	INDENT(2);
	error_print(err);
	UNDENT(2);

	error_free(err);
	iop_free(iop);
	return MHD_queue_response(conn, MHD_HTTP_OK, web_reply_json(msg));
}

enum MHD_Result web_new_user(struct MHD_Connection *conn) {
	const char *prev_uuid;
	const char *realname;
	struct MHD_Response *resp;
	uuid_t uuid;
	struct valkey_t *vk;
	struct valkeyReply *reply;
	error_t *ret;
	char msg[128];
	struct user_t user = {0};

	// @todo display user uuid if they've lost it
	prev_uuid = MHD_lookup_connection_value(conn, MHD_COOKIE_KIND, "userid");
	if (prev_uuid)
		return web_bad_arg(conn, "userid");

	realname = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "name");
	if (!realname || strlen(realname) < 3)
		return web_bad_arg(conn, "name");

	// realname already zero initialized, guaranteed null terminator at USER_NAME_LEN
	strncpy(user.realname, realname, USER_NAME_LEN);

	vk = get_valkey();
	reply = valkeyCommand(vk->ctx, "HGET usernames %s", user.realname);
	if (!reply || reply->type == VALKEY_REPLY_ERROR) {
		ret = E_VALKEY(vk->ctx, reply);
		goto fail_valkey;
	}

	if (reply->type != VALKEY_REPLY_NIL) {
		ret = E_MSG("username taken");
		goto fail_msg;
	}

	freeReplyObject(reply);
	reply = valkeyCommand(vk->ctx, "INCR next_user");
	if (!reply || reply->type == VALKEY_REPLY_ERROR)
		goto fail_valkey;

	user.id = reply->integer;
	uuid_generate(uuid);
	uuid_unparse(uuid, user.name);

	freeReplyObject(reply);
	reply = valkeyCommand(vk->ctx, "HSETNX usernames %s %s", user.realname, user.name);
	if (!reply || reply->type == VALKEY_REPLY_ERROR) {
		ret = E_VALKEY(vk->ctx, reply);
		goto fail_valkey;
	}

	DEBUG("initialized new user %s (%s) id %u\n", user.name, user.realname, user.id);

	freeReplyObject(reply);
	reply = valkeyCommand(vk->ctx, "HSET userids %d %s", user.id, user.name);
	if (!reply || reply->type == VALKEY_REPLY_ERROR)
		goto fail_valkey;

	freeReplyObject(reply);
	reply = valkeyCommand(vk->ctx, "HSET %s id %d name %s", user.name, user.id,
		user.realname);
	if (!reply || reply->type == VALKEY_REPLY_ERROR)
		goto fail_valkey;

	snprintf(msg, sizeof(msg), "{\"uuid\":\"%s\"}", user.name);
	resp = web_reply_json(msg);
	web_set_cookie(resp, "userid", user.name);
	web_set_cookie(resp, "userdisplay", user.realname);

	freeReplyObject(reply);
	release_valkey(vk);
	return MHD_queue_response(conn, MHD_HTTP_OK, resp);

fail_valkey:
	ret = E_VALKEY(vk->ctx, reply);
fail_msg:
	freeReplyObject(reply);
	release_valkey(vk);
	return web_send_error(conn, ret);
}

enum MHD_Result web_new_game(struct MHD_Connection *conn) {
	uuid_t userid;
	const char *user_arg;
	const char *type_arg;
	int type;
	error_t *ret;
	struct MHD_Response *resp;
	struct game_t game = {0};
	struct user_t user = {0};
	char msg[128];

	user_arg = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "user");
	if (!user_arg || uuid_parse(user_arg, userid) < 0)
		return web_bad_arg(conn, "user");

	type_arg = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "type");
	if (!type_arg)
		return web_bad_arg(conn, "type");

	type = atoi(type_arg);
	if (!valid_game_type((size_t) type))
		return web_bad_arg(conn, "type");

	// Require uuid so that you cannot start games as someone else
	if (!find_user(userid, &user)) {
		DEBUG("could not find user for (valid) uuid %s\n", user_arg);
		return web_bad_arg(conn, "user");
	}

	ret = new_game(type, &user, &game);
	if (NOT_OK(ret))
		return web_send_error(conn, ret);

	DEBUG("new game %s, type %u\n", game.name, type);
	snprintf(msg, sizeof(msg), "{\"id\":\"%s\"}", game.name);
	release_game(&game);

	resp = web_reply_json(msg);
	return MHD_queue_response(conn, MHD_HTTP_OK, resp);
}

void format_game(char *buf, size_t len, struct game_t *game) {
	const char *status = "running";

	if (game_is_finished(game)) {
		if (game->goals_satisfied)
			status = "completed";
		else
			status = "failed";
		snprintf(buf, len, "{\"status\":\"%s\",\"count\":%d}", status, game->count);
	}
	else {
		snprintf(buf, len, "{\"status\":\"%s\",\"count\":%d,\"next\":%d}",
			status, game->count, game->next);
	}
}

enum MHD_Result web_process_person(struct MHD_Connection *conn) {
	uuid_t gameid;
	bool verdict = false;
	int person;
	const char *verdict_arg;
	const char *game_arg;
	const char *person_arg;
	struct MHD_Response *resp;
	error_t *ret;
	char msg[128];
	struct game_t game = {0};

	game_arg = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "game");
	if (!game_arg || uuid_parse(game_arg, gameid) < 0)
		return web_bad_arg(conn, "game");

	person_arg = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "person");
	if (!person_arg)
		return web_bad_arg(conn, "person");

	ret = find_game(gameid, &game);
	if (NOT_OK(ret)) {
		DEBUG("could not find game for (valid) uuid %s\n", game_arg);
		return web_bad_arg(conn, "game");
	}

	verdict_arg = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "verdict");
	if (!verdict_arg)
		goto send_reply;

	if (!STRING_EQUALS(verdict_arg, "false"))
		verdict = true;

	person = atoi(person_arg);

	if (person != (int) game.count) {
		ret = E_MSG("wrong person");
		goto handle_error;
	}

	ret = process_next_person(&game, verdict);
	if (NOT_OK(ret))
		goto handle_error;

	if (!game_is_finished(&game)) {
		ret = create_next_person(&game);
		if (NOT_OK(ret))
			goto handle_error;
	}

send_reply:
	format_game(msg, sizeof(msg), &game);
	resp = web_reply_json(msg);
	release_game(&game);
	return MHD_queue_response(conn, MHD_HTTP_OK, resp);

handle_error:
	release_game(&game);
	return web_send_error(conn, ret);
}

enum MHD_Result web_process_game_details(struct MHD_Connection *conn) {
	const char *game_arg;
	struct MHD_Response *resp;
	struct ioport *iop;
	error_t *ret;
	char msg[256];
	struct game_t game = {0};

	game_arg = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "game");
	if (!game_arg)
		return web_bad_arg(conn, "game");

	ret = find_game_string(game_arg, &game);
	if (NOT_OK(ret)) {
		error_free(ret);
		return web_bad_arg(conn, "game");
	}

	iop = iop_alloc_fixstr(msg, sizeof(msg));

	iop_printf(iop,
		"{\"count\":%u,\"accepted\":%u,\"next\":%u,\"attrs\":[",
		game.count, game.accepted, game.next);
	for (size_t i = 0; i < MAX_ATTRS-1; ++i) {
		iop_printf(iop, "%u,", game.attr_n[i]);
	}
	iop_printf(iop, "%u],\"type\":%d", game.attr_n[MAX_ATTRS-1], game.type);

	if (game_is_finished(&game)) {
		iop_printf(iop, ",\"finished\":true,\"won\":%s",
			game.goals_satisfied ? "true" : "false");
	}

	iop_printf(iop, "}");

	iop_free(iop);
	release_game(&game);

	resp = web_reply_json(msg);
	return MHD_queue_response(conn, MHD_HTTP_OK, resp);
}

enum MHD_Result web_symbols(struct MHD_Connection *conn) {
	const char *game_arg;
	struct MHD_Response *resp;
	struct ioport *iop;
	char *msg;
	size_t msglen;
	size_t i;
	error_t *ret;
	struct game_t game = {0};

	game_arg = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "game");
	if (!game_arg)
		return web_bad_arg(conn, "game");

	ret = find_game_string(game_arg, &game);
	if (NOT_OK(ret)) {
		error_free(ret);
		return web_bad_arg(conn, "game");
	}

	// Approximate guess at buffer size, make it larger if there are failures
	// Note that each symbol is a uint8_t currently so we need at most 3 digits
	// plus a space (=4) for each one
	msglen = 128 + 4*game.count;
	msg = calloc(msglen, sizeof(*msg));
	iop = iop_alloc_fixstr(msg, msglen);

	iop_printf(iop, "{\"count\":%d,\"symbols\":[", game.count);
	if (game.count > 0) {
		for (i = 0; i < game.count-1; ++i) {
			iop_printf(iop, "%d,", game.seen[i]);
		}
		iop_printf(iop, "%d]}", game.seen[game.count-1]);
	}
	else {
		iop_printf(iop, "]}");
	}

	resp = web_reply_json(msg);
	free(msg);
	iop_free(iop);
	release_game(&game);

	return MHD_queue_response(conn, MHD_HTTP_OK, resp);
}

/**
 * Retrieve either number of game rule sets available or the rules for a specific
 * type of game
 */
enum MHD_Result web_params(struct MHD_Connection *conn) {
	const char *type_arg;
	size_t n, i, j, buflen;
	size_t np;
	char *buf;
	struct ioport *iop;
	int type;
	struct game_params_t *params;
	struct MHD_Response *resp;

	type_arg = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "type");
	if (!type_arg) {
		char msg[128];
		snprintf(msg, sizeof(msg), "{\"rulesets\":%zu}", get_number_of_games());
		return MHD_queue_response(conn, MHD_HTTP_OK, web_reply_json(msg));
	}

	type = atoi(type_arg);
	params = get_game_params(type);
	if (!params)
		return web_bad_arg(conn, "type");

	// Approximate buffer length expected to hold a set of game parameters, if
	// the games get bigger make this estimate larger:
	// up to 9 digits per double value + comma, with n marginals and n^2 covariances
	// up to 6 digits per goal parameter
	// + 64 bytes for labels + json padding
	np = 0;
	for (i = 0; i < params->n_goals; ++i) {
		j = 0;
		while (!is_flag_set(params->goals[i].params[j], GOAL_TAIL_BIT)) {
			j += 1;
			np += 1;
		}
	}

	n = params->rng_params.n;
	buflen = 64 + 9*n + 9*n*n + 6*np;
	buf = calloc(buflen, sizeof(*buf));
	iop = iop_alloc_fixstr(buf, buflen);

	iop_printf(iop, "{\"type\":%d,\"p\":[", type);
	for (i = 0; i < n-1; ++i) {
		iop_printf(iop, "%0.6f,", params->dist_params.marginals[i]);
	}
	iop_printf(iop, "%0.6f],\"Q\":[", params->dist_params.marginals[n-1]);

	for (i = 0; i < n*n-1; ++i) {
		iop_printf(iop, "%0.6f,", params->dist_params.corr[i]);
	}
	iop_printf(iop, "%0.6f],\"goals\":[", params->dist_params.corr[n*n-1]);

	for (i = 0; i < params->n_goals; ++i) {
		if (i > 0)
			iop_printf(iop, ",[");
		else
			iop_printf(iop, "[");

		j = 0;
		while (!is_flag_set(params->goals[i].params[j+1], GOAL_TAIL_BIT)) {
			iop_printf(iop, "%d,", params->goals[i].params[j]);
			j += 1;
		}
		iop_printf(iop, "%d]", params->goals[i].params[j]);
	}
	iop_printf(iop, "]}");

	iop_free(iop);
	resp = web_reply_json(buf);
	free(buf);
	return MHD_queue_response(conn, MHD_HTTP_OK, resp);
}

enum MHD_Result web_gameid(struct MHD_Connection *conn) {
	struct valkey_t *vk;
	valkeyReply *reply;
	char msg[128];

	vk = get_valkey();
	reply = valkeyCommand(vk->ctx, "GET next_game");
	if (!reply || reply->type == VALKEY_REPLY_ERROR) {
		error_t *ret = E_VALKEY(vk->ctx, reply);
		freeReplyObject(reply);
		release_valkey(vk);
		return web_send_error(conn, ret);
	}

	snprintf(msg, sizeof(msg), "{\"gameid\":%s}", reply->str);
	freeReplyObject(reply);
	release_valkey(vk);
	return MHD_queue_response(conn, MHD_HTTP_OK, web_reply_json(msg));
}

error_t *describe_game(struct ioport *iop, uint32_t id) {
	struct game_t game = {0};
	error_t *ret;

	ret = find_game_by_id(id, &game);
	if (NOT_OK(ret)) {
		error_free(ret);
		iop_printf(iop, "{}");
		return OK;
	}

	iop_printf(iop,
		"{\"id\":%u,\"count\":%u,\"accepted\":%u,",
		game.id, game.count, game.accepted);

	if (game_is_finished(&game)) {
		iop_printf(iop, "\"finished\":true,\"won\":%s}",
			game.goals_satisfied ? "true" : "false");
	}
	else {
		iop_printf(iop, "\"finished\":false}");
	}

	release_game(&game);
	return OK;
}

enum MHD_Result web_recent_games(struct MHD_Connection *conn) {
	struct valkey_t *vk;
	valkeyReply *reply;
	char *buf;
	size_t buflen;
	struct ioport *iop;
	struct MHD_Response *resp;
	error_t *ret;
	int n, i;

	vk = get_valkey();
	reply = valkeyCommand(vk->ctx, "GET next_game");
	if (!reply || reply->type == VALKEY_REPLY_ERROR) {
		error_t *ret = E_VALKEY(vk->ctx, reply);
		freeReplyObject(reply);
		release_valkey(vk);
		return web_send_error(conn, ret);
	}

	// same approach as in user games just fixed number of recent games
	buflen = 64 + 64*RECENT_GAME_LIMIT;
	buf = calloc(buflen, sizeof(*buf));
	if (!buf)
		goto failure_noiop;

	iop = iop_alloc_fixstr(buf, buflen);
	if (!iop)
		goto failure_nobuf;

	n = atoi(reply->str);
	iop_printf(iop, "{\"games\":[");
	if (n > 0) {
		for (i = 0; i < RECENT_GAME_LIMIT && n - i >= 1; ++i) {
			ret = describe_game(iop, n - i);
			if (NOT_OK(ret))
				goto failure;
			iop_printf(iop, ",");
		}

		ret = describe_game(iop, n - i);
		if (NOT_OK(ret))
			goto failure;
	}
	iop_printf(iop, "]}");

	iop_free(iop);
	resp = web_reply_json(buf);
	free(buf);
	freeReplyObject(reply);
	release_valkey(vk);
	return MHD_queue_response(conn, MHD_HTTP_OK, resp);

failure:
	iop_free(iop);
failure_noiop:
	free(buf);
failure_nobuf:
	freeReplyObject(reply);
	release_valkey(vk);
	return web_send_error(conn, ret);
}

enum MHD_Result web_user_games(struct MHD_Connection *conn) {
	const char *user_arg;
	struct valkey_t *vk;
	valkeyReply *reply;
	char *buf;
	size_t buflen;
	struct ioport *iop;
	struct MHD_Response *resp;
	error_t *ret;
	struct user_t user = {0};

	user_arg = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "name");
	if (!user_arg)
		return web_bad_arg(conn, "name");

	if (!find_user_by_string(user_arg, &user))
		return web_bad_arg(conn, "name");

	vk = get_valkey();
	reply = valkeyCommand(vk->ctx, "LRANGE %s-games 0 -1", user.name);
	if (!reply || reply->type == VALKEY_REPLY_ERROR) {
		error_t *ret = E_VALKEY(vk->ctx, reply);
		freeReplyObject(reply);
		release_valkey(vk);
		return web_send_error(conn, ret);
	}

	// number of games * some descriptor length guess + scaffolding
	buflen = 64 + 64*reply->elements;
	buf = calloc(buflen, sizeof(*buf));
	if (!buf)
		goto failure_nobuf;

	iop = iop_alloc_fixstr(buf, buflen);
	if (!iop)
		goto failure_noiop;

	iop_printf(iop, "{\"games\":[");
	if (reply->elements > 0) {
		for (size_t i = 0; i < reply->elements-1; ++i) {
			ret = describe_game(iop, atoi(reply->element[i]->str));
			if (NOT_OK(ret))
				goto failure;
			iop_printf(iop, ",");
		}
		ret = describe_game(iop, atoi(reply->element[reply->elements-1]->str));
		if (NOT_OK(ret))
			goto failure;
	}
	iop_printf(iop, "]}");

	iop_free(iop);
	resp = web_reply_json(buf);
	free(buf);
	freeReplyObject(reply);
	release_valkey(vk);
	return MHD_queue_response(conn, MHD_HTTP_OK, resp);

failure:
	iop_free(iop);
failure_noiop:
	free(buf);
failure_nobuf:
	freeReplyObject(reply);
	release_valkey(vk);
	return web_send_error(conn, ret);
}

/**
 * Handle a new request, each of these is called in its own thread by the
 * MHD internals for now
 */
enum MHD_Result web_entry(void *context, struct MHD_Connection *conn, const char *url,
	const char *method, const char *version, const char *upload, size_t *upload_size,
	void **state)
{
	UNUSED(context);
	UNUSED(version);
	UNUSED(upload);
	UNUSED(upload_size);
	UNUSED(state);

	if (!STRING_EQUALS(method, "GET"))
		return MHD_NO;

	if (STRING_EQUALS(url, "/new-user"))
		return web_new_user(conn);

	if (STRING_EQUALS(url, "/new-game"))
		return web_new_game(conn);

	if (STRING_EQUALS(url, "/process-person"))
		return web_process_person(conn);

	if (STRING_EQUALS(url, "/details"))
		return web_process_game_details(conn);

	if (STRING_EQUALS(url, "/symbols"))
		return web_symbols(conn);

	if (STRING_EQUALS(url, "/params"))
		return web_params(conn);

	if (STRING_EQUALS(url, "/gameid"))
		return web_gameid(conn);

	if (STRING_EQUALS(url, "/user-games"))
		return web_user_games(conn);

	if (STRING_EQUALS(url, "/recent-games"))
		return web_recent_games(conn);

	DEBUG("failed to match any routes for %s\n", url);
	return MHD_NO;
}

/**
 * Clear the valkey storage entirely, resetting all identifiers, usernames, keys, etc.
 * You should ensure that we're not accepting any requests when this is done, probably
 * with an rwlock around using valkey at all if this is intended to be available live,
 * but it is probably more useful as a startup option to reset once before launching
 */
void reinit_db(void) {
	struct valkey_t *vk;
	valkeyReply *reply;
	valkeyReply *inner;

	vk = get_valkey();
	reply = valkeyCommand(vk->ctx, "KEYS *");
	if (!reply || reply->type == VALKEY_REPLY_ERROR) {
		ERROR("valkey failure during reinitialization, fatal\n");
		exit(1);
	}

	for (size_t i = 0; i < reply->elements; ++i) {
		inner = valkeyCommand(vk->ctx, "DEL %s", reply->element[i]->str);
		if (!inner || inner->type == VALKEY_REPLY_ERROR) {
			ERROR("failed to delete key %s, fatal\n", reply->element[i]->str);
			exit(1);
		}
		freeReplyObject(inner);
	}

	freeReplyObject(reply);
	release_valkey(vk);
}

void show_help(void) {
	printf("\n");
	printf(" berghain-server [-h] [-r]\n");
	printf("\n");
	printf("   -h      Show this help\n");
	printf("   -r      Reset valkey database (removes ALL keys)\n");
	printf("\n");
	exit(1);
}

int main(int argc, char **argv) {
	char c;
	int opt;
	bool reset = false;
	struct MHD_Daemon *daemon;
	error_t *ret;

	while ((opt = getopt(argc, argv, "hr")) != -1) {
		switch (opt) {
		case 'h': /* fallthrough */
		default:
			show_help();
			break;
		case 'r':
			DEBUG("reset valkey database\n");
			reset = true;
			break;
		}
	}

	ret = init_game();
	if (NOT_OK(ret)) {
		error_print(ret);
		exit(1);
	}

	if (reset)
		reinit_db();

	daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, GAME_PORT, NULL, NULL,
		&web_entry, NULL, MHD_OPTION_END);
	if (!daemon) {
		ERROR("failed to start mhd daemon\n");
		exit(1);
	}

	DEBUG("web server active, game is ready\n");

	while ((c = getchar())) {
		if (c == 'q')
			break;
	}

	MHD_stop_daemon(daemon);
	return 0;
}
