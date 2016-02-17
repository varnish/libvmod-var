#include <stdlib.h>
#include <ctype.h>
#include <sys/socket.h>

#include "vrt.h"
#include "vsa.h"
#include "cache/cache.h"

#include "vcc_if.h"

enum VAR_TYPE {
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
		VCL_IP IP;
	} value;
	VTAILQ_ENTRY(var) list;
};

struct var_head {
	unsigned magic;
#define VAR_HEAD_MAGIC 0x64F33E2F
	uint32_t vxid;
	VTAILQ_HEAD(, var) vars;
};

static unsigned implicit_clears_disabled = 0;
static struct var_head **var_list = NULL;
static int var_list_sz = 0;
static VTAILQ_HEAD(, var) global_vars = VTAILQ_HEAD_INITIALIZER(global_vars);
static pthread_mutex_t var_list_mtx = PTHREAD_MUTEX_INITIALIZER;

static void
vh_init(struct var_head *vh)
{

	AN(vh);
	memset(vh, 0, sizeof *vh);
	vh->magic = VAR_HEAD_MAGIC;
	VTAILQ_INIT(&vh->vars);
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
			return v;
	}
	return NULL;
}

static struct var *
vh_get_var_alloc(struct var_head *vh, const char *name, const struct vrt_ctx *ctx)
{
	struct var *v;

	v = vh_get_var(vh, name);

	if (!v) {
		/* Allocate and add */
		v = (struct var*)WS_Alloc(ctx->ws, sizeof(struct var));
		AN(v);
		v->magic = VAR_MAGIC;
		v->name = WS_Copy(ctx->ws, name, -1);
		AN(v->name);
		VTAILQ_INSERT_HEAD(&vh->vars, v, list);
	}
	return v;
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
			var_list[i] = malloc(sizeof(struct var_head));
			vh_init(var_list[i]);
		}
	}
	AZ(pthread_mutex_unlock(&var_list_mtx));
	return 0;
}

static struct var_head *
get_vh(const struct vrt_ctx *ctx)
{
	struct var_head *vh;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(ctx->vsl);

	AZ(pthread_mutex_lock(&var_list_mtx));
	while (var_list_sz <= ctx->req->sp->fd) {
		int ns = var_list_sz*2;
		/* resize array */
		var_list = realloc(var_list, ns * sizeof(struct var_head *));
		for (; var_list_sz < ns; var_list_sz++) {
			var_list[var_list_sz] = malloc(sizeof(struct var_head));
			vh_init(var_list[var_list_sz]);
		}
		assert(var_list_sz == ns);
		AN(var_list);
	}
	vh = var_list[ctx->req->sp->fd];

	if (!implicit_clears_disabled) {
		AN(ctx->req->vsl->wid);
		if (vh->vxid != ctx->vsl->wid) {
			vh_init(vh);
			vh->vxid = ctx->vsl->wid;
		}
	}
	AZ(pthread_mutex_unlock(&var_list_mtx));
	return vh;
}

VCL_VOID
vmod_set(const struct vrt_ctx *ctx, VCL_STRING name, VCL_STRING value)
{
	vmod_set_string(ctx, name, value);
}

VCL_STRING
vmod_get(const struct vrt_ctx *ctx, VCL_STRING name)
{
	return vmod_get_string(ctx, name);
}

VCL_VOID
vmod_set_string(const struct vrt_ctx *ctx, VCL_STRING name, VCL_STRING value)
{
	struct var *v;

	if (name == NULL)
		return;
	v = vh_get_var_alloc(get_vh(ctx), name, ctx);
	AN(v);
	v->type = STRING;
	if (value == NULL)
		value = "";
	v->value.STRING = WS_Copy(ctx->ws, value, -1);
}

VCL_STRING
vmod_get_string(const struct vrt_ctx *ctx, VCL_STRING name)
{
	struct var *v;
	if (name == NULL)
		return (NULL);
	v = vh_get_var(get_vh(ctx), name);
	if (!v || v->type != STRING)
		return NULL;
	return (v->value.STRING);
}

VCL_VOID
vmod_set_ip(const struct vrt_ctx *ctx, VCL_STRING name, VCL_IP ip)
{
	struct var *v;

	if (name == NULL)
		return;
	v = vh_get_var_alloc(get_vh(ctx), name, ctx);
	AN(v);
	v->type = IP;
	AN(ip);
	v->value.IP = WS_Copy(ctx->ws, ip, vsa_suckaddr_len);;
}

VCL_IP
vmod_get_ip(const struct vrt_ctx *ctx, VCL_STRING name)
{
	struct var *v;
	if (name == NULL)
		return (NULL);
	v = vh_get_var(get_vh(ctx), name);
	if (!v || v->type != IP)
		return NULL;
	return (v->value.IP);
}

#define VMOD_SET_X(vcl_type_u, vcl_type_l, ctype)			\
VCL_VOID								\
vmod_set_##vcl_type_l(const struct vrt_ctx *ctx,			\
    const char *name, ctype value)					\
{									\
	struct var *v;							\
	if (name == NULL)						\
		return;							\
	v = vh_get_var_alloc(get_vh(ctx), name, ctx);			\
	AN(v);								\
	v->type = vcl_type_u;						\
	v->value.vcl_type_u = value;					\
}

VMOD_SET_X(INT, int, VCL_INT)
VMOD_SET_X(REAL, real, VCL_REAL)
VMOD_SET_X(DURATION, duration, VCL_DURATION)

#define VMOD_GET_X(vcl_type_u, vcl_type_l, ctype)			\
ctype									\
vmod_get_##vcl_type_l(const struct vrt_ctx *ctx, const char *name)	\
{									\
	struct var *v;							\
									\
	if (name == NULL)						\
		return 0;						\
	v = vh_get_var(get_vh(ctx), name);				\
									\
	if (!v || v->type != vcl_type_u)				\
		return 0;						\
	return (v->value.vcl_type_u);					\
}

VMOD_GET_X(INT, int, VCL_INT)
VMOD_GET_X(REAL, real, VCL_REAL)
VMOD_GET_X(DURATION, duration, VCL_DURATION)

VCL_VOID
vmod_clear(const struct vrt_ctx *ctx)
{
	struct var_head *vh;
	vh = get_vh(ctx);
	vh_init(vh);
}

VCL_VOID
vmod_global_set(const struct vrt_ctx *ctx, VCL_STRING name, VCL_STRING value)
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

VCL_STRING
vmod_global_get(const struct vrt_ctx *ctx, VCL_STRING name)
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
		r = WS_Copy(ctx->ws, v->value.STRING, -1);
		AN(r);
	}
	AZ(pthread_mutex_unlock(&var_list_mtx));
	return(r);
}

VCL_VOID
vmod_disable_implicit_clears(const struct vrt_ctx *ctx)
{
	implicit_clears_disabled = 1;
}
