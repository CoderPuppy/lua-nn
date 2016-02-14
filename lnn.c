#include<lua.h>
#include<lualib.h>
#include<lauxlib.h>
#include<nanomsg/nn.h>
#include<nanomsg/pubsub.h>
#include<stdlib.h>
#include<stdbool.h>

#define LNN_SOCKET "lnn-socket"
#define LNN_POLL "lnn-poll"

void lnn_register_socket(lua_State*);
void lnn_register_poll(lua_State*);

static int lnn_socket(lua_State*);
static int lnn_poll(lua_State*);

static const luaL_Reg lnn_fns[] = {
	{ "socket", lnn_socket },
	{ "poll", lnn_poll },
	NULL
};

LUALIB_API int luaopen_nn(lua_State *L) {
#if LUA_VERSION_NUM >= 502
	luaL_newlib(L, lnn_fns);
#else
	luaL_register(L, "nn", lnn_fns);
#endif

	{
		int value;
		const char *name;

		for(int n = 0; (name = nn_symbol(n, &value)) != NULL; n++) {
			lua_pushinteger(L, value);
			lua_setfield(L, -2, name);
		}
	};

	lnn_register_socket(L);
	lnn_register_poll(L);

	return 1;
}

static int lnn_socket(lua_State *L) {
	int domain = luaL_checkinteger(L, 1);
	int protocol = luaL_checkinteger(L, 2);

	int s = nn_socket(domain, protocol);
	if(s == -1)
		luaL_error(L, "%s", nn_strerror(nn_errno()));

	*((int*) lua_newuserdata(L, sizeof(int))) = s;
	luaL_getmetatable(L, LNN_SOCKET);
	lua_setmetatable(L, -2);

	return 1;
}

struct lnn_poll {
	size_t len;
	struct nn_pollfd fds;
};

static int lnn_poll(lua_State *L) {
	struct lnn_poll *poll = malloc(sizeof(size_t));
	poll->len = 0;
	struct lnn_poll **ptr = lua_newuserdata(L, sizeof(struct lnn_poll*));
	*ptr = poll;
	luaL_getmetatable(L, LNN_POLL);
	lua_setmetatable(L, -2);
	return 1;
}

static int lnn_socket_close(lua_State*);

static int lnn_socket_bind(lua_State*);
static int lnn_socket_connect(lua_State*);
static int lnn_socket_shutdown(lua_State*);

static int lnn_socket_recv(lua_State*);
static int lnn_socket_send(lua_State*);

static int lnn_socket_setopt(lua_State*);

static const luaL_Reg lnn_socket_fns[] = {
	{ "close", lnn_socket_close },

	{ "bind", lnn_socket_bind },
	{ "connect", lnn_socket_connect },
	{ "shutdown", lnn_socket_shutdown },

	{ "recv", lnn_socket_recv },
	{ "send", lnn_socket_send },

	{ "setopt", lnn_socket_setopt },
	NULL
};

void lnn_register_socket(lua_State *L) {
	if (luaL_newmetatable(L, LNN_SOCKET)) {
#if LUA_VERSION_NUM >= 502
		luaL_setfuncs(L, lnn_socket_fns, 0);
#else
		luaL_register(L, NULL, lnn_socket_fns);
#endif
		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, lnn_socket_close);
		lua_settable(L, -3);

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, -2);
		lua_settable(L, -3);

		lua_pushliteral(L, "__metatable");
		lua_pushliteral(L, "must not access this metatable");
		lua_settable(L, -3);
	}
	lua_pop(L, 1);
}

static int lnn_socket_close(lua_State *L) {
	int s = *((int*) luaL_checkudata(L, 1, LNN_SOCKET));

	if(nn_close(s) == -1)
		luaL_error(L, "%s", nn_strerror(nn_errno()));

	return 0;
}

static int lnn_socket_bind(lua_State *L) {
	int s = *((int*) luaL_checkudata(L, 1, LNN_SOCKET));
	const char *addr = luaL_checkstring(L, 2);

	int endpoint = nn_bind(s, addr);
	if(endpoint == -1)
		luaL_error(L, "%s", nn_strerror(nn_errno()));

	lua_pushinteger(L, endpoint);

	return 1;
}

static int lnn_socket_connect(lua_State *L) {
	int s = *((int*) luaL_checkudata(L, 1, LNN_SOCKET));
	const char *addr = luaL_checkstring(L, 2);

	int endpoint = nn_connect(s, addr);
	if(endpoint == -1)
		luaL_error(L, "%s", nn_strerror(nn_errno()));

	lua_pushinteger(L, endpoint);

	return 1;
}

static int lnn_socket_shutdown(lua_State *L) {
	int s = *((int*) luaL_checkudata(L, 1, LNN_SOCKET));
	int endpoint = luaL_checkinteger(L, 2);

	if(nn_shutdown(s, endpoint) == -1)
		luaL_error(L, "%s", nn_strerror(nn_errno()));

	return 0;
}

static int lnn_socket_recv(lua_State *L) {
	int s = *((int*) luaL_checkudata(L, 1, LNN_SOCKET));

	int flags = 0;
	if(lua_gettop(L) >= 3 && lua_isinteger(L, 3)) {
		flags = luaL_checkinteger(L, 3);
	}

	if(lua_gettop(L) >= 2 && lua_isinteger(L, 2)) {
		int buf_len = luaL_checkinteger(L, 2);
		char *buf = malloc(buf_len);
		int recv_len = nn_recv(s, buf, buf_len, flags);
		if(recv_len == -1) {
			int err = nn_errno();
			if(errno & EAGAIN)
				return 0;
			else
				luaL_error(L, "%s", nn_strerror(nn_errno()));
		}
		lua_pushlstring(L, buf, recv_len);

		free(buf);
		return 1;
	} else {
		char *buf;
		int recv_len = nn_recv(s, &buf, NN_MSG, flags);
		if(recv_len == -1) {
			int err = nn_errno();
			if(errno & EAGAIN)
				return 0;
			else
				luaL_error(L, "%s", nn_strerror(err));
		}
		lua_pushlstring(L, buf, recv_len);
		nn_freemsg(buf);
		return 1;
	}
}

static int lnn_socket_send(lua_State *L) {
	int s = *((int*) luaL_checkudata(L, 1, LNN_SOCKET));
	size_t len;
	const char *msg = luaL_checklstring(L, 2, &len);

	int flags = 0;
	if(lua_gettop(L) >= 3 && lua_isinteger(L, 3)) {
		flags = luaL_checkinteger(L, 3);
	}

	if(nn_send(s, msg, len, flags) == 1) {
		int err = nn_errno();
		if(errno & EAGAIN)
			return 0;
		else
			luaL_error(L, "%s", nn_strerror(nn_errno()));
	}

	lua_pushinteger(L, len);

	return 1;
}

static int lnn_socket_setopt(lua_State *L) {
	int s = *((int*) luaL_checkudata(L, 1, LNN_SOCKET));

	int level = luaL_checkinteger(L, 2);
	int option = luaL_checkinteger(L, 3);

	size_t len;
	const char *val;

	if(lua_isstring(L, 4)) {
		val = luaL_checklstring(L, 4, &len);
	} else if(lua_isinteger(L, 4)) {
		int ival = luaL_checkinteger(L, 4);
		val = (char*) &ival;
		len = sizeof(ival);
	}

	if(nn_setsockopt(s, level, option, val, len) == -1)
		luaL_error(L, "%s", nn_strerror(nn_errno()));

	return 0;
}

static int lnn_poll_close(lua_State*);

static int lnn_poll_add(lua_State*);

static int lnn_poll_poll(lua_State*);

static int lnn_poll_inp(lua_State*);
static int lnn_poll_out(lua_State*);

static const luaL_Reg lnn_poll_fns[] = {
	{ "close", lnn_poll_close },

	{ "add", lnn_poll_add },

	{ "poll", lnn_poll_poll },

	{ "inp", lnn_poll_inp },
	{ "out", lnn_poll_out },

	NULL
};

void lnn_register_poll(lua_State *L) {
	if (luaL_newmetatable(L, LNN_POLL)) {
#if LUA_VERSION_NUM >= 502
		luaL_setfuncs(L, lnn_poll_fns, 0);
#else
		luaL_register(L, NULL, lnn_poll_fns);
#endif
		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, lnn_poll_close);
		lua_settable(L, -3);

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, -2);
		lua_settable(L, -3);

		lua_pushliteral(L, "__metatable");
		lua_pushliteral(L, "must not access this metatable");
		lua_settable(L, -3);
	}
	lua_pop(L, 1);
}

static int lnn_poll_close(lua_State *L) {
	struct lnn_poll *poll = *((struct lnn_poll**) luaL_checkudata(L, 1, LNN_POLL));

	free(poll);

	return 0;
}

static int lnn_poll_add(lua_State *L) {
	struct lnn_poll **ptr = luaL_checkudata(L, 1, LNN_POLL);
	struct lnn_poll *poll = *ptr;
	int s = *((int*) luaL_checkudata(L, 2, LNN_SOCKET));
	bool inp, out = false;
	if(lua_gettop(L) >= 3) {
		inp = lua_toboolean(L, 3);
		if(lua_gettop(L) >= 4) {
			out = lua_toboolean(L, 4);
		}
	}

	poll->len++;
	poll = realloc(poll, sizeof(size_t) + poll->len * sizeof(struct nn_pollfd));
	*ptr = poll;

	(&poll->fds)[poll->len - 1].fd = s;
	(&poll->fds)[poll->len - 1].events = (inp && NN_POLLIN) | (out && NN_POLLOUT);

	lua_pushinteger(L, poll->len - 1);

	return 1;
}

static int lnn_poll_poll(lua_State *L) {
	struct lnn_poll *poll = *((struct lnn_poll**) luaL_checkudata(L, 1, LNN_POLL));
	int timeout = -1;
	if(lua_gettop(L) >= 2 && lua_isinteger(L, 2))
		timeout = lua_tointeger(L, 2);

	int r = nn_poll(&poll->fds, poll->len, timeout);

	if(r == -1)
		luaL_error(L, "%s", nn_strerror(nn_errno()));

	lua_pushinteger(L, r);

	return 1;
}

static int lnn_poll_inp(lua_State *L) {
	struct lnn_poll *poll = *((struct lnn_poll**) luaL_checkudata(L, 1, LNN_POLL));
	int i = luaL_checkinteger(L, 2);

	if(i < 0 || i >= poll->len)
		luaL_error(L, "invalid index for pollfd: %d", i);

	lua_pushboolean(L, (&poll->fds)[i].revents & NN_POLLIN);

	return 1;
}

static int lnn_poll_out(lua_State *L) {
	struct lnn_poll *poll = *((struct lnn_poll**) luaL_checkudata(L, 1, LNN_POLL));
	int i = luaL_checkinteger(L, 2);

	if(i < 0 || i >= poll->len)
		luaL_error(L, "invalid index for pollfd: %d", i);

	lua_pushboolean(L, (&poll->fds)[i].revents & NN_POLLOUT);

	return 1;
}
