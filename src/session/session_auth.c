#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Session crypto context rotation.
 * Reallocates the crypto context buffer for key rotation.
 */
int vmx_session_rotate_context(vmx_session_t *sess, const vmx_parsed_frame_t *f) {
    if (!sess || !f) return VMX_ERR_INVALID_FRAME;
    if (!sess->initialized || !sess->negotiated)
        return VMX_ERR_INVALID_STATE;
    if (!(sess->capabilities & VMX_CAP_CRYPTO))
        return VMX_ERR_INVALID_STATE;

    /* Validate rekey subrecord */
    if (f->parsed_subrecord_count < 1) return VMX_ERR_INVALID_FRAME;
    const vmx_subrecord_t *sub = &f->subrecords[0];
    if (sub->kind != VMX_SUB_CRYPTO_CTX) return VMX_ERR_INVALID_FRAME;
    if (sub->generation != sess->crypto_generation)
        return VMX_ERR_INVALID_STATE;

    /* Free old crypto context */
    vmx_free(sess->crypto_ctx);

    /* Allocate new context with updated key material */
    size_t new_len = sess->crypto_ctx_len;
    if (sub->payload_len >= 2) {
        uint16_t req_len = vmx_read_u16(sub->payload);
        if (req_len >= 32 && req_len <= 256)
            new_len = req_len;
    }

    sess->crypto_ctx = vmx_alloc(new_len);
    if (!sess->crypto_ctx) {
        sess->crypto_ctx_len = 0;
        return VMX_ERR_NOMEM;
    }
    sess->crypto_ctx_len = new_len;
    sess->crypto_generation++;

    /* Derive new key material from session state */
    uint32_t seed = vmx_hash_mix(sess->session_id, sess->crypto_generation);
    for (size_t i = 0; i < new_len; i++) {
        seed = vmx_hash_mix(seed, (uint32_t)i);
        sess->crypto_ctx[i] = (uint8_t)(seed & 0xFF);
    }

    return VMX_OK;
}
