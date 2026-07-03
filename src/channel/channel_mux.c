#include "../common/types.h"
#include "../common/alloc.h"

vmx_channel_t *vmx_channel_mux_alloc(vmx_session_t *sess, uint32_t channel_id) {
    if (!sess) return NULL;
    if (sess->channel_count >= VMX_MAX_CHANNELS) return NULL;
    if (!(sess->capabilities & VMX_CAP_CHANNEL)) return NULL;

    /* Check for duplicate */
    for (uint16_t i = 0; i < sess->channel_count; i++) {
        if (sess->channels[i] && sess->channels[i]->channel_id == channel_id)
            return NULL;
    }

    vmx_channel_t *ch = vmx_calloc(1, sizeof(vmx_channel_t));
    if (!ch) return NULL;

    ch->channel_id = channel_id;
    ch->session_id = sess->session_id;
    ch->state = VMX_LIFECYCLE_CREATED;

    sess->channels[sess->channel_count++] = ch;
    return ch;
}

int vmx_channel_mux_free(vmx_session_t *sess, vmx_channel_t *ch) {
    if (!sess || !ch) return VMX_ERR_INVALID_STATE;

    /* Remove from session registry */
    for (uint16_t i = 0; i < sess->channel_count; i++) {
        if (sess->channels[i] == ch) {
            sess->channels[i] = NULL;
            break;
        }
    }

    vmx_free(ch);
    return VMX_OK;
}

vmx_channel_t *vmx_channel_lookup(vmx_session_t *sess, uint32_t channel_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->channel_count; i++) {
        if (sess->channels[i] && sess->channels[i]->channel_id == channel_id)
            return sess->channels[i];
    }
    return NULL;
}
