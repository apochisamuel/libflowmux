#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_route_id = 1;

int vmx_route_advertise(vmx_session_t *sess, uint32_t span_id,
                        uint32_t partition_id, uint32_t node_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    if (sess->route_count >= VMX_MAX_ROUTES) return VMX_ERR_FULL;

    vmx_span_t *span = vmx_span_registry_lookup(sess, span_id);
    if (!span) return VMX_ERR_NOT_FOUND;

    vmx_route_entry_t *entry = vmx_calloc(1, sizeof(vmx_route_entry_t));
    if (!entry) return VMX_ERR_NOMEM;

    entry->route_id = next_route_id++;
    entry->span_id = span_id;
    entry->partition_id = partition_id;
    entry->node_id = node_id;
    entry->resolved_span = span;
    entry->generation = span->generation;
    entry->active = true;

    span->route_id = entry->route_id;

    sess->routes[sess->route_count++] = entry;
    return VMX_OK;
}

int vmx_route_withdraw(vmx_session_t *sess, uint32_t route_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    vmx_route_entry_t *e = vmx_route_lookup(sess, route_id);
    if (!e) return VMX_ERR_NOT_FOUND;
    e->active = false;
    return VMX_OK;
}

vmx_span_t *vmx_route_resolve_span(vmx_session_t *sess, uint32_t route_id) {
    if (!sess) return NULL;
    vmx_route_entry_t *e = vmx_route_lookup(sess, route_id);
    if (!e || !e->active) return NULL;
    
    return e->resolved_span;
}

int vmx_route_update_mapping(vmx_session_t *sess, uint32_t route_id,
                             uint32_t new_span_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    vmx_route_entry_t *e = vmx_route_lookup(sess, route_id);
    if (!e) return VMX_ERR_NOT_FOUND;

    vmx_span_t *new_span = vmx_span_registry_lookup(sess, new_span_id);
    if (!new_span) return VMX_ERR_NOT_FOUND;

    e->span_id = new_span_id;
    e->resolved_span = new_span;
    e->generation++;

    return VMX_OK;
}

int vmx_route_add_node(vmx_session_t *sess, uint32_t node_id,
                       const char *addr, uint16_t port) {
    if (!sess) return VMX_ERR_INVALID_STATE;
    if (sess->node_count >= VMX_MAX_NODES) return VMX_ERR_FULL;

    vmx_node_desc_t *node = vmx_calloc(1, sizeof(vmx_node_desc_t));
    if (!node) return VMX_ERR_NOMEM;

    node->node_id = node_id;
    if (addr) {
        size_t len = strlen(addr);
        if (len >= sizeof(node->addr)) len = sizeof(node->addr) - 1;
        memcpy(node->addr, addr, len);
    }
    node->port = port;
    node->active = true;

    sess->nodes[sess->node_count++] = node;
    return VMX_OK;
}

int vmx_route_remove_node(vmx_session_t *sess, uint32_t node_id) {
    if (!sess) return VMX_ERR_INVALID_STATE;

    for (uint8_t i = 0; i < sess->node_count; i++) {
        if (sess->nodes[i] && sess->nodes[i]->node_id == node_id) {
            /* BUG: does not notify replication to purge references */
            vmx_free(sess->nodes[i]);
            sess->nodes[i] = NULL;
            return VMX_OK;
        }
    }
    return VMX_ERR_NOT_FOUND;
}

vmx_route_entry_t *vmx_route_lookup(vmx_session_t *sess, uint32_t route_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->route_count; i++) {
        if (sess->routes[i] && sess->routes[i]->route_id == route_id)
            return sess->routes[i];
    }
    return NULL;
}

vmx_node_desc_t *vmx_node_lookup(vmx_session_t *sess, uint32_t node_id) {
    if (!sess) return NULL;
    for (uint8_t i = 0; i < sess->node_count; i++) {
        if (sess->nodes[i] && sess->nodes[i]->node_id == node_id)
            return sess->nodes[i];
    }
    return NULL;
}

int vmx_route_handle_advertise(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t span_id = f->route_id;
    uint32_t partition_id = 0, node_id = 0;
    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_ROUTE_ENTRY && sub->payload_len >= 8) {
            partition_id = vmx_read_u32(sub->payload);
            node_id = vmx_read_u32(sub->payload + 4);
        }
    }
    return vmx_route_advertise(sess, span_id, partition_id, node_id);
}

int vmx_route_handle_withdraw(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t route_id = f->route_id;
    return vmx_route_withdraw(sess, route_id);
}

int vmx_route_handle_rebalance(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    /* Rebalance triggers route update + migration */
    uint32_t route_id = f->route_id;
    (void)route_id;
    return VMX_OK;
}

int vmx_route_handle_node_join(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t node_id = 0;
    uint16_t port = 0;
    char addr[64] = {0};
    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_NODE_DESC && sub->payload_len >= 8) {
            node_id = vmx_read_u32(sub->payload);
            port = vmx_read_u16(sub->payload + 4);
            size_t alen = sub->payload_len - 6;
            if (alen > 63) alen = 63;
            memcpy(addr, sub->payload + 6, alen);
        }
    }
    if (node_id == 0) return VMX_ERR_INVALID_ID;
    return vmx_route_add_node(sess, node_id, addr, port);
}

int vmx_route_handle_node_depart(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    uint32_t node_id = 0;
    if (f->parsed_subrecord_count > 0) {
        const vmx_subrecord_t *sub = &f->subrecords[0];
        if (sub->kind == VMX_SUB_NODE_DESC && sub->payload_len >= 4) {
            node_id = vmx_read_u32(sub->payload);
        }
    }
    if (node_id == 0) return VMX_ERR_INVALID_ID;
    return vmx_route_remove_node(sess, node_id);
}
