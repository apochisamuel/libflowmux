#include "../common/types.h"
#include "../common/crc.h"

/*
 * Deep frame validation pipeline.
 * Checks structural, semantic, and policy constraints beyond
 * basic CRC and format validation.
 */

typedef enum {
    VMX_VAL_OK            = 0,
    VMX_VAL_BAD_TYPE      = 1,
    VMX_VAL_BAD_SUBRECORD = 2,
    VMX_VAL_BAD_ORDERING  = 3,
    VMX_VAL_BAD_SIZE      = 4,
    VMX_VAL_BAD_SEMANTIC  = 5,
    VMX_VAL_POLICY_DENY   = 6
} vmx_validation_code_t;

typedef struct {
    vmx_validation_code_t code;
    uint8_t               frame_type;
    uint8_t               subrecord_idx;
    const char            *message;
} vmx_validation_result_t;

/*
 * Validate frame type ordering: certain frame types must precede others.
 * E.g., SESSION_INIT must come before any other frame type.
 */
int vmx_validate_ordering(const vmx_session_t *sess,
                          const vmx_parsed_frame_t *f,
                          vmx_validation_result_t *result) {
    if (!sess || !f || !result) return -1;

    result->code = VMX_VAL_OK;
    result->frame_type = f->frame_type;

    /* Session must be initialized before any non-init frame */
    if (f->frame_type != VMX_FRAME_SESSION_INIT && !sess->initialized) {
        result->code = VMX_VAL_BAD_ORDERING;
        result->message = "session not initialized";
        return -1;
    }

    /* Capabilities must be negotiated before subsystem frames */
    if (!sess->negotiated) {
        switch (f->frame_type) {
        case VMX_FRAME_SESSION_INIT:
        case VMX_FRAME_SESSION_CAP_NEG:
        case VMX_FRAME_SESSION_CLOSE:
            break;  /* allowed before negotiation */
        default:
            result->code = VMX_VAL_BAD_ORDERING;
            result->message = "capabilities not negotiated";
            return -1;
        }
    }

    /* Cap-neg must come exactly once, after init */
    if (f->frame_type == VMX_FRAME_SESSION_CAP_NEG && sess->negotiated) {
        result->code = VMX_VAL_BAD_ORDERING;
        result->message = "duplicate capability negotiation";
        return -1;
    }

    return 0;
}

/*
 * Validate subrecord type compatibility with frame type.
 */
int vmx_validate_subrecord_types(const vmx_parsed_frame_t *f,
                                 vmx_validation_result_t *result) {
    if (!f || !result) return -1;
    result->code = VMX_VAL_OK;

    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const vmx_subrecord_t *sub = &f->subrecords[i];
        bool valid = true;

        switch (f->frame_type) {
        case VMX_FRAME_SPAN_CREATE:
        case VMX_FRAME_SPAN_META_UPDATE:
            valid = (sub->kind == VMX_SUB_META ||
                     sub->kind == VMX_SUB_GEN_STAMP);
            break;
        case VMX_FRAME_SEGMENT_ATTACH:
        case VMX_FRAME_SEGMENT_DETACH:
            valid = (sub->kind == VMX_SUB_SEGMENT_REF ||
                     sub->kind == VMX_SUB_DATA);
            break;
        case VMX_FRAME_LEASE_ACQUIRE:
        case VMX_FRAME_LEASE_RELEASE:
        case VMX_FRAME_LEASE_RENEW:
            valid = (sub->kind == VMX_SUB_LEASE_GRANT);
            break;
        case VMX_FRAME_CURSOR_OPEN:
        case VMX_FRAME_CURSOR_ADVANCE:
            valid = (sub->kind == VMX_SUB_CURSOR_POS);
            break;
        case VMX_FRAME_STREAM_APPEND:
            valid = (sub->kind == VMX_SUB_DATA);
            break;
        case VMX_FRAME_REPL_ADD_MEMBER:
        case VMX_FRAME_REPL_REMOVE_MEMBER:
            valid = (sub->kind == VMX_SUB_REPLICA_DESC);
            break;
        case VMX_FRAME_MIGRATE_INITIATE:
            valid = (sub->kind == VMX_SUB_MIGRATION_STATE);
            break;
        case VMX_FRAME_ROUTE_ADVERTISE:
            valid = (sub->kind == VMX_SUB_ROUTE_ENTRY);
            break;
        case VMX_FRAME_ROUTE_NODE_JOIN:
        case VMX_FRAME_ROUTE_NODE_DEPART:
            valid = (sub->kind == VMX_SUB_NODE_DESC);
            break;
        case VMX_FRAME_PARTITION_ADD:
        case VMX_FRAME_PARTITION_REMOVE:
        case VMX_FRAME_CHANNEL_BIND:
        case VMX_FRAME_CHANNEL_REMAP:
            valid = (sub->kind == VMX_SUB_PARTITION_DESC);
            break;
        case VMX_FRAME_BP_CREDIT_UPDATE:
        case VMX_FRAME_BP_WINDOW_ADJUST:
            valid = (sub->kind == VMX_SUB_CREDIT_GRANT);
            break;
        case VMX_FRAME_SESSION_REKEY:
            valid = (sub->kind == VMX_SUB_CRYPTO_CTX);
            break;
        default:
            valid = true;  /* unknown frame type: accept any subrecord */
        }

        if (!valid) {
            result->code = VMX_VAL_BAD_SUBRECORD;
            result->subrecord_idx = i;
            result->frame_type = f->frame_type;
            result->message = "incompatible subrecord kind for frame type";
            return -1;
        }
    }

    return 0;
}

/*
 * Validate capability requirements: check that the session has
 * the required capabilities for the given frame type.
 */
int vmx_validate_capabilities(const vmx_session_t *sess,
                              const vmx_parsed_frame_t *f,
                              vmx_validation_result_t *result) {
    if (!sess || !f || !result) return -1;
    result->code = VMX_VAL_OK;

    uint32_t required = 0;

    switch (f->frame_type) {
    case VMX_FRAME_CHANNEL_OPEN:
    case VMX_FRAME_CHANNEL_CLOSE:
    case VMX_FRAME_CHANNEL_BIND:
    case VMX_FRAME_CHANNEL_REMAP:
    case VMX_FRAME_CHANNEL_DRAIN:
        required = VMX_CAP_CHANNEL;
        break;
    case VMX_FRAME_STREAM_OPEN:
    case VMX_FRAME_STREAM_CLOSE:
    case VMX_FRAME_STREAM_APPEND:
    case VMX_FRAME_STREAM_READ:
    case VMX_FRAME_STREAM_FLUSH:
        required = VMX_CAP_STREAM;
        break;
    case VMX_FRAME_CURSOR_OPEN:
    case VMX_FRAME_CURSOR_CLOSE:
    case VMX_FRAME_CURSOR_ADVANCE:
    case VMX_FRAME_CURSOR_READ:
        required = VMX_CAP_CURSOR;
        break;
    case VMX_FRAME_REPL_SETUP:
    case VMX_FRAME_REPL_ADD_MEMBER:
    case VMX_FRAME_REPL_REMOVE_MEMBER:
    case VMX_FRAME_REPL_SYNC_BEGIN:
    case VMX_FRAME_REPL_SYNC_DATA:
    case VMX_FRAME_REPL_CHECK:
        required = VMX_CAP_REPLICATION;
        break;
    case VMX_FRAME_MIGRATE_INITIATE:
    case VMX_FRAME_MIGRATE_PREPARE:
    case VMX_FRAME_MIGRATE_TRANSFER:
    case VMX_FRAME_MIGRATE_HANDOFF:
    case VMX_FRAME_MIGRATE_ABORT:
        required = VMX_CAP_MIGRATION;
        break;
    case VMX_FRAME_CHECKPOINT_BEGIN:
    case VMX_FRAME_CHECKPOINT_FLUSH:
    case VMX_FRAME_CHECKPOINT_RESTORE:
        required = VMX_CAP_CHECKPOINT;
        break;
    case VMX_FRAME_RETENTION_EVAL:
    case VMX_FRAME_RETENTION_COLLECT:
        required = VMX_CAP_RETENTION;
        break;
    case VMX_FRAME_BP_CREDIT_UPDATE:
    case VMX_FRAME_BP_WINDOW_ADJUST:
        required = VMX_CAP_BACKPRESSURE;
        break;
    case VMX_FRAME_SESSION_REKEY:
        required = VMX_CAP_CRYPTO;
        break;
    default:
        required = 0;
    }

    if (required != 0 && !(sess->capabilities & required)) {
        result->code = VMX_VAL_POLICY_DENY;
        result->frame_type = f->frame_type;
        result->message = "missing required capability";
        return -1;
    }

    return 0;
}

/*
 * Full validation pipeline: runs all checks in order.
 * Returns 0 on success, -1 on first validation failure.
 */
int vmx_validate_frame_full(const vmx_session_t *sess,
                            const vmx_parsed_frame_t *f,
                            vmx_validation_result_t *result) {
    if (!sess || !f || !result) return -1;

    int rc;

    rc = vmx_validate_ordering(sess, f, result);
    if (rc != 0) return rc;

    rc = vmx_validate_capabilities(sess, f, result);
    if (rc != 0) return rc;

    rc = vmx_validate_subrecord_types(f, result);
    if (rc != 0) return rc;

    return 0;
}
