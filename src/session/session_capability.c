#include "../common/types.h"

/*
 * Session capability negotiation state machine and
 * capability dependency matrix.
 *
 * Some capabilities depend on others:
 *   STREAM requires CHANNEL
 *   CURSOR requires STREAM
 *   MIGRATION requires REPLICATION
 *   CHECKPOINT requires RETENTION
 *   CRYPTO is independent
 *   BACKPRESSURE requires CHANNEL
 */

typedef struct {
    uint32_t capability;
    uint32_t dependencies;
    const char *name;
} vmx_cap_entry_t;

static const vmx_cap_entry_t cap_table[] = {
    { VMX_CAP_CHANNEL,      0,                                       "channel" },
    { VMX_CAP_STREAM,       VMX_CAP_CHANNEL,                         "stream" },
    { VMX_CAP_CURSOR,       VMX_CAP_STREAM | VMX_CAP_CHANNEL,        "cursor" },
    { VMX_CAP_REPLICATION,  0,                                       "replication" },
    { VMX_CAP_MIGRATION,    VMX_CAP_REPLICATION,                     "migration" },
    { VMX_CAP_CHECKPOINT,   VMX_CAP_RETENTION,                       "checkpoint" },
    { VMX_CAP_RETENTION,    0,                                       "retention" },
    { VMX_CAP_BACKPRESSURE, VMX_CAP_CHANNEL,                         "backpressure" },
    { VMX_CAP_CRYPTO,       0,                                       "crypto" },
};

#define VMX_CAP_TABLE_SIZE (sizeof(cap_table) / sizeof(cap_table[0]))

/*
 * Resolve capability dependencies: given requested capabilities,
 * ensure all dependencies are also included.
 */
uint32_t vmx_cap_resolve_deps(uint32_t requested) {
    uint32_t resolved = requested;
    bool changed = true;

    /* Fixed-point iteration until no new deps are added */
    while (changed) {
        changed = false;
        for (size_t i = 0; i < VMX_CAP_TABLE_SIZE; i++) {
            if ((resolved & cap_table[i].capability) &&
                (resolved & cap_table[i].dependencies) != cap_table[i].dependencies) {
                resolved |= cap_table[i].dependencies;
                changed = true;
            }
        }
    }
    return resolved;
}

/*
 * Validate that a set of capabilities is consistent
 * (all dependencies satisfied).
 */
int vmx_cap_validate(uint32_t capabilities) {
    for (size_t i = 0; i < VMX_CAP_TABLE_SIZE; i++) {
        if ((capabilities & cap_table[i].capability) &&
            (capabilities & cap_table[i].dependencies) != cap_table[i].dependencies) {
            return -1;
        }
    }
    return 0;
}

/*
 * Compute the intersection of two capability sets.
 * Used during negotiation to find common capabilities.
 */
uint32_t vmx_cap_negotiate(uint32_t offered, uint32_t requested) {
    uint32_t common = offered & requested;
    /* Resolve dependencies on the common set */
    return vmx_cap_resolve_deps(common);
}

/*
 * Count active capabilities in a bitmask.
 */
int vmx_cap_count(uint32_t capabilities) {
    int count = 0;
    for (size_t i = 0; i < VMX_CAP_TABLE_SIZE; i++) {
        if (capabilities & cap_table[i].capability) count++;
    }
    return count;
}

/*
 * Get capability name by flag.
 */
const char *vmx_cap_name(uint32_t cap_flag) {
    for (size_t i = 0; i < VMX_CAP_TABLE_SIZE; i++) {
        if (cap_table[i].capability == cap_flag)
            return cap_table[i].name;
    }
    return "unknown";
}

/*
 * Negotiation state machine.
 */
typedef enum {
    VMX_NEG_INITIAL     = 0,
    VMX_NEG_OFFERED     = 1,
    VMX_NEG_COUNTERED   = 2,
    VMX_NEG_ACCEPTED    = 3,
    VMX_NEG_REJECTED    = 4
} vmx_neg_state_t;

typedef struct {
    vmx_neg_state_t state;
    uint32_t        local_offer;
    uint32_t        remote_offer;
    uint32_t        agreed;
    uint8_t         round;
    uint8_t         max_rounds;
} vmx_neg_ctx_t;

int vmx_neg_init(vmx_neg_ctx_t *ctx, uint32_t local_caps) {
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->local_offer = vmx_cap_resolve_deps(local_caps);
    ctx->max_rounds = 3;
    ctx->state = VMX_NEG_INITIAL;
    return 0;
}

int vmx_neg_send_offer(vmx_neg_ctx_t *ctx) {
    if (!ctx || ctx->state == VMX_NEG_ACCEPTED) return -1;
    ctx->state = VMX_NEG_OFFERED;
    ctx->round++;
    return 0;
}

int vmx_neg_receive_offer(vmx_neg_ctx_t *ctx, uint32_t remote_caps) {
    if (!ctx) return -1;
    ctx->remote_offer = vmx_cap_resolve_deps(remote_caps);

    uint32_t common = vmx_cap_negotiate(ctx->local_offer, ctx->remote_offer);
    if (common == ctx->local_offer || ctx->round >= ctx->max_rounds) {
        ctx->agreed = common;
        ctx->state = VMX_NEG_ACCEPTED;
    } else {
        ctx->state = VMX_NEG_COUNTERED;
        ctx->local_offer = common;
    }
    return 0;
}

bool vmx_neg_is_complete(const vmx_neg_ctx_t *ctx) {
    return ctx && (ctx->state == VMX_NEG_ACCEPTED || ctx->state == VMX_NEG_REJECTED);
}
