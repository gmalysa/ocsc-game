#include <ctype.h>
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
	reply = valkeyCommand(vk->ctx, "HSETNX usernames %s %s", user.realname, user.name);
	if (!reply || reply->type == VALKEY_REPLY_ERROR) {
		ret = E_VALKEY(vk->ctx, reply);
		goto fail_valkey;
	}

	// HSETNX returns 0 on failure, 1 on success
	if (!reply->integer) {
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
	resp = MHD_create_response_from_buffer(strlen(msg), msg, MHD_RESPMEM_MUST_COPY);
	MHD_add_response_header(resp, "Content-Type", "application/json");

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
	if (!valid_game_type(type))
		return web_bad_arg(conn, "type");

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
	}

	snprintf(buf, len, "{\"status\":\"%s\",\"count\":%d,\"next\":%d}",
		status, game->count, game->next);
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
	uuid_t gameid;
	const char *game_arg;
	struct MHD_Response *resp;
	struct ioport *iop;
	error_t *ret;
	char msg[256];
	struct game_t game = {0};

	game_arg = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "game");
	if (!game_arg)
		return web_bad_arg(conn, "game");

 	if (uuid_parse(game_arg, gameid) == 0) {
		ret = find_game(gameid, &game);
		if (NOT_OK(ret)) {
			error_free(ret);
			return web_bad_arg(conn, "game");
		}
	}
	else {
		uint32_t id = atoi(game_arg);
		ret = find_game_by_id(id, &game);
		if (NOT_OK(ret)) {
			error_free(ret);
			return web_bad_arg(conn, "game");
		}
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

	if (STRING_EQUALS(url, "/game/new-user"))
		return web_new_user(conn);

	if (STRING_EQUALS(url, "/game/new-game"))
		return web_new_game(conn);

	if (STRING_EQUALS(url, "/game/process-person"))
		return web_process_person(conn);

	if (STRING_EQUALS(url, "/game/details"))
		return web_process_game_details(conn);

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
	// @todo collect things to reset
	// del userids, usernames, next_user
	// iterate keys for uuids and delete each of those
	// iterate keys for uuid-m and delete each of those
}

int main(int argc, char **argv) {
	char c;
	struct MHD_Daemon *daemon;
	error_t *ret;

	UNUSED(argc);
	UNUSED(argv);

	ret = init_game();
	if (NOT_OK(ret)) {
		error_print(ret);
		exit(1);
	}

	daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD,  8124, NULL, NULL,
		&web_entry, NULL, MHD_OPTION_END);
	if (!daemon) {
		ERROR("failed to start mhd daemon\n");
		exit(1);
	}

	while ((c = getchar())) {
		if (c == 'q')
			break;
	}

	MHD_stop_daemon(daemon);
	return 0;
}
