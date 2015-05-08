#include <stdlib.h>
#include <ctype.h>

#include "vrt.h"
#include "bin/varnishd/cache.h"

#include "vcc_if.h"

enum VAR_TYPE {
	UNSET,
	STRING,
	INT,
	REAL,
	DURATION,
	IP
};

struct var {
	unsigned magic;
#define VAR_MAGIC 0x8A21A651
	char *name;
	enum VAR_TYPE type;
	union {
		char *STRING;
		int INT;
		double REAL;
		double DURATION;
		struct sockaddr_storage IP;
	} value;
	VTAILQ_ENTRY(var) list;
};

struct var_head {
	unsigned magic;
#define VAR_HEAD_MAGIC 0x64F33E2F
	unsigned xid;
	VTAILQ_HEAD(, var) vars;
};

static struct var_head **var_list = NULL;
static int var_list_sz = 0;
static VTAILQ_HEAD(, var) global_vars = VTAILQ_HEAD_INITIALIZER(global_vars);
static pthread_mutex_t var_list_mtx = PTHREAD_MUTEX_INITIALIZER;

static void
var_clean(struct var *v)
{

	CHECK_OBJ_NOTNULL(v, VAR_MAGIC);
	switch (v->type) {
	case STRING:
		AN(v->value.STRING);
		free(v->value.STRING);
		break;
	default:
		break;
	}
	v->type = UNSET;
}

static void
vh_reset(struct var_head *vh)
{
	struct var *v;

	CHECK_OBJ_NOTNULL(vh, VAR_HEAD_MAGIC);
	while (!VTAILQ_EMPTY(&vh->vars)) {
		v = VTAILQ_FIRST(&vh->vars);
		VTAILQ_REMOVE(&vh->vars, v, list);
		var_clean(v);
		AN(v->name);
		free(v->name);
		FREE_OBJ(v);
	}
}

static struct var *
vh_get_var(struct var_head *vh, const char *name)
{
	struct var *v;

	AN(vh);
	AN(name);
	VTAILQ_FOREACH(v, &vh->vars, list) {
		CHECK_OBJ_NOTNULL(v, VAR_MAGIC);
		AN(v->name);
		if (strcmp(v->name, name) == 0)
			return (v);
	}
	return (NULL);
}

static struct var *
vh_get_var_alloc(struct var_head *vh, const char *name, struct sess *sp)
{
	struct var *v;

	v = vh_get_var(vh, name);

	if (!v) {
		/* Allocate and add */
		ALLOC_OBJ(v, VAR_MAGIC);
		v->type = UNSET;
		v->name = strdup(name);
		AN(v->name);
		VTAILQ_INSERT_HEAD(&vh->vars, v, list);
	}
	return (v);
}

int
init_function(struct vmod_priv *priv, const struct VCL_conf *conf)
{

	AZ(pthread_mutex_lock(&var_list_mtx));
	if (var_list == NULL) {
		AZ(var_list_sz);
		var_list_sz = 256;
		var_list = malloc(sizeof(struct var_head *) * 256);
		AN(var_list);
		for (int i = 0 ; i < var_list_sz; i++) {
			ALLOC_OBJ(var_list[i], VAR_HEAD_MAGIC);
			VTAILQ_INIT(&var_list[i]->vars);
		}
	}
	AZ(pthread_mutex_unlock(&var_list_mtx));
	return 0;
}

static struct var_head *
get_vh(struct sess *sp)
{
	struct var_head *vh;

	AZ(pthread_mutex_lock(&var_list_mtx));
	while (var_list_sz <= sp->id) {
		int ns = var_list_sz*2;
		/* resize array */
		var_list = realloc(var_list, ns * sizeof(struct var_head *));
		for (; var_list_sz < ns; var_list_sz++) {
			ALLOC_OBJ(var_list[var_list_sz], VAR_HEAD_MAGIC);
			VTAILQ_INIT(&var_list[var_list_sz]->vars);
		}
		assert(var_list_sz == ns);
		AN(var_list);
	}
	vh = var_list[sp->id];

	if (vh->xid != sp->xid) {
		vh_reset(vh);
		vh->xid = sp->xid;
	}
	AZ(pthread_mutex_unlock(&var_list_mtx));
	return vh;
}

void
vmod_set(struct sess *sp, const char *name, const char *value)
{
	vmod_set_string(sp, name, value);
}

const char *
vmod_get(struct sess *sp, const char *name)
{
	return vmod_get_string(sp, name);
}

void
vmod_set_string(struct sess *sp, const char *name, const char *value)
{
	struct var *v;

	if (name == NULL)
		return;
	v = vh_get_var_alloc(get_vh(sp), name, sp);
	AN(v);
	var_clean(v);
	v->type = STRING;
	if (value == NULL)
		value = "";
	v->value.STRING = strdup(value);
}

const char *
vmod_get_string(struct sess *sp, const char *name)
{
	struct var *v;
	if (name == NULL)
		return (NULL);
	v = vh_get_var(get_vh(sp), name);
	if (!v || v->type != STRING)
		return (NULL);
	return (v->value.STRING);
}

void
vmod_set_ip(struct sess *sp, const char *name, struct sockaddr_storage *ip)
{
	struct var *v;

	if (name == NULL)
		return;
	v = vh_get_var_alloc(get_vh(sp), name, sp);
	AN(v);
	var_clean(v);
	v->type = IP;
	AN(ip);
	v->value.IP = *ip;
}

struct sockaddr_storage *
vmod_get_ip(struct sess *sp, const char *name)
{
	struct var *v;
	if (name == NULL)
		return (NULL);
	v = vh_get_var(get_vh(sp), name);
	if (!v || v->type != IP)
		return (NULL);
	return (&v->value.IP);
}

#define VMOD_SET_X(vcl_type_u, vcl_type_l, ctype)			\
void									\
vmod_set_##vcl_type_l(struct sess *sp, const char *name, ctype value)	\
{									\
	struct var *v;							\
	if (name == NULL)						\
		return;							\
	v = vh_get_var_alloc(get_vh(sp), name, sp);			\
	AN(v);								\
	var_clean(v);							\
	v->type = vcl_type_u;						\
	v->value.vcl_type_u = value;					\
}

VMOD_SET_X(INT, int, int)
VMOD_SET_X(REAL, real, double)
VMOD_SET_X(DURATION, duration, double)

#define VMOD_GET_X(vcl_type_u, vcl_type_l, ctype)			\
ctype									\
vmod_get_##vcl_type_l(struct sess *sp, const char *name)		\
{									\
	struct var *v;							\
									\
	if (name == NULL)						\
		return (0);						\
	v = vh_get_var(get_vh(sp), name);				\
									\
	if (!v || v->type != vcl_type_u)				\
		return (0);						\
	return (v->value.vcl_type_u);					\
}

VMOD_GET_X(INT, int, int)
VMOD_GET_X(REAL, real, double)
VMOD_GET_X(DURATION, duration, double)

void vmod_clear(struct sess *sp)
{
	struct var_head *vh;
	vh = get_vh(sp);
	CHECK_OBJ_NOTNULL(vh, VAR_HEAD_MAGIC);
	vh_reset(vh);
}

void
vmod_global_set(struct sess *sp, const char *name, const char *value)
{
	struct var *v;

	if (name == NULL)
		return;

	AZ(pthread_mutex_lock(&var_list_mtx));
	VTAILQ_FOREACH(v, &global_vars, list) {
		CHECK_OBJ_NOTNULL(v, VAR_MAGIC);
		AN(v->name);
		if (strcmp(v->name, name) == 0)
			break;
	}
	if (v) {
		VTAILQ_REMOVE(&global_vars, v, list);
		free(v->name);
		v->name = NULL;
	} else
		ALLOC_OBJ(v, VAR_MAGIC);
	AN(v);
	v->name = strdup(name);
	AN(v->name);
	VTAILQ_INSERT_HEAD(&global_vars, v, list);
	if (v->type == STRING)
		free(v->value.STRING);
	v->value.STRING = NULL;
	v->type = STRING;
	if (value != NULL)
		v->value.STRING = strdup(value);

	AZ(pthread_mutex_unlock(&var_list_mtx));
}

const char *
vmod_global_get(struct sess *sp, const char *name)
{
	struct var *v;
	const char *r = NULL;

	AZ(pthread_mutex_lock(&var_list_mtx));
	VTAILQ_FOREACH(v, &global_vars, list) {
		CHECK_OBJ_NOTNULL(v, VAR_MAGIC);
		AN(v->name);
		if (strcmp(v->name, name) == 0)
			break;
	}
	if (v && v->value.STRING != NULL) {
		r = WS_Dup(sp->ws, v->value.STRING);
		AN(r);
	}
	AZ(pthread_mutex_unlock(&var_list_mtx));
	return (r);
}
