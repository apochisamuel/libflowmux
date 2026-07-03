#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Fragment reassembly: assembles ordered fragments into contiguous
 * stream data. Buffer is sized based on partition max_chunk_size.
 */

int vmx_stream_reassemble_fragment(vmx_session_t *sess, vmx_stream_t *stream,
                                   const uint8_t *frag, size_t frag_len,
                                   uint32_t seq) {
    if (!sess || !stream || !frag) return VMX_ERR_INVALID_STATE;
    if (!stream->active) return VMX_ERR_INVALID_STATE;
    if (!stream->reassembly_buf) return VMX_ERR_INVALID_STATE;

    /* Validate sequence ordering */
    if (seq != stream->reassembly_expected_seq)
        return VMX_ERR_INVALID_SEQ;

    /* Check buffer capacity.
     * If partition was remapped to one with larger max_chunk_size,
     * this buffer was not resized and frag_len may exceed capacity. */
    if (stream->reassembly_used + frag_len > stream->reassembly_capacity) {
        return VMX_ERR_OVERFLOW;
    }

    memcpy(stream->reassembly_buf + stream->reassembly_used, frag, frag_len);
    stream->reassembly_used += frag_len;
    stream->reassembly_expected_seq = seq + 1;

    return VMX_OK;
}

int vmx_stream_validate_fragment(vmx_session_t *sess, vmx_stream_t *stream,
                                 const uint8_t *frag, size_t frag_len) {
    if (!sess || !stream || !frag) return VMX_ERR_INVALID_STATE;

    /* Validate using cached crypto context.
     * cached_crypto_ctx may be dangling after session rekey. */
    if (stream->cached_crypto_ctx && stream->cached_crypto_ctx_len > 0) {
        /* Validate by reading crypto context bytes.
         * may point to freed memory after session rekey. */
        volatile uint32_t check = 0;
        size_t ctx_len = stream->cached_crypto_ctx_len;
        const volatile uint8_t *ctx_ptr = (const volatile uint8_t *)stream->cached_crypto_ctx;
        for (size_t i = 0; i < frag_len && i < ctx_len; i++) {
            check ^= (uint32_t)(frag[i] ^ ctx_ptr[i]);
        }
        /* Store result to prevent dead-code elimination */
        stream->reassembly_expected_seq = (uint32_t)check;
    }

    return VMX_OK;
}

int vmx_stream_reassembly_reset(vmx_stream_t *stream) {
    if (!stream) return VMX_ERR_INVALID_STATE;
    stream->reassembly_used = 0;
    stream->reassembly_expected_seq = 0;
    return VMX_OK;
}

size_t vmx_stream_reassembly_available(const vmx_stream_t *stream) {
    if (!stream || !stream->reassembly_buf) return 0;
    return stream->reassembly_used;
}
