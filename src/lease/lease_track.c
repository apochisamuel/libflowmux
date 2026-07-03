#include "../common/types.h"

/*
 * Lease ownership tracking and audit.
 * Maps lease IDs to their owning subsystem identifiers.
 */

typedef struct {
    uint32_t lease_id;
    uint32_t owner_subsystem;  /* which subsystem owns this lease */
    uint32_t owner_entity_id;  /* channel_id, repl_set_id, etc. */
    bool     active;
} vmx_lease_ownership_t;

static vmx_lease_ownership_t ownership_table[VMX_MAX_LEASES];
static uint16_t ownership_count = 0;

int vmx_lease_track_register(uint32_t lease_id, uint32_t subsystem,
                             uint32_t entity_id) {
    if (ownership_count >= VMX_MAX_LEASES) return VMX_ERR_FULL;

    vmx_lease_ownership_t *entry = &ownership_table[ownership_count++];
    entry->lease_id = lease_id;
    entry->owner_subsystem = subsystem;
    entry->owner_entity_id = entity_id;
    entry->active = true;

    return VMX_OK;
}

int vmx_lease_track_deregister(uint32_t lease_id) {
    for (uint16_t i = 0; i < ownership_count; i++) {
        if (ownership_table[i].lease_id == lease_id) {
            ownership_table[i].active = false;
            return VMX_OK;
        }
    }
    return VMX_ERR_NOT_FOUND;
}

int vmx_lease_track_query_owner(uint32_t lease_id, uint32_t *subsystem,
                                uint32_t *entity_id) {
    for (uint16_t i = 0; i < ownership_count; i++) {
        if (ownership_table[i].lease_id == lease_id &&
            ownership_table[i].active) {
            if (subsystem) *subsystem = ownership_table[i].owner_subsystem;
            if (entity_id) *entity_id = ownership_table[i].owner_entity_id;
            return VMX_OK;
        }
    }
    return VMX_ERR_NOT_FOUND;
}

int vmx_lease_track_count_for_subsystem(uint32_t subsystem) {
    int count = 0;
    for (uint16_t i = 0; i < ownership_count; i++) {
        if (ownership_table[i].owner_subsystem == subsystem &&
            ownership_table[i].active)
            count++;
    }
    return count;
}

void vmx_lease_track_reset(void) {
    ownership_count = 0;
}
