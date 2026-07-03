#include "error.h"
const char *vmx_error_string(vmx_error_t err) {
    switch (err) {
    case VMX_OK:               return "success";
    case VMX_ERR_NOMEM:        return "out of memory";
    case VMX_ERR_INVALID_MAGIC: return "invalid magic";
    case VMX_ERR_INVALID_CRC:  return "CRC mismatch";
    case VMX_ERR_INVALID_SEQ:  return "sequence mismatch";
    case VMX_ERR_INVALID_EPOCH: return "epoch mismatch";
    case VMX_ERR_INVALID_FRAME: return "invalid frame";
    case VMX_ERR_INVALID_ID:   return "invalid identifier";
    case VMX_ERR_INVALID_STATE: return "invalid state";
    case VMX_ERR_INVALID_TOKEN: return "invalid token";
    case VMX_ERR_INVALID_LEASE: return "invalid lease";
    case VMX_ERR_NOT_FOUND:    return "not found";
    case VMX_ERR_FULL:         return "registry full";
    case VMX_ERR_EXPIRED:      return "expired";
    case VMX_ERR_FROZEN:       return "frozen";
    case VMX_ERR_DUPLICATE:    return "duplicate";
    case VMX_ERR_PERMISSION:   return "permission denied";
    case VMX_ERR_OVERFLOW:     return "overflow";
    case VMX_ERR_UNDERFLOW:    return "underflow";
    case VMX_ERR_TRUNCATED:    return "truncated";
    case VMX_ERR_PROTOCOL:     return "protocol error";
    case VMX_ERR_MIGRATION:    return "migration error";
    case VMX_ERR_REPLICATION:  return "replication error";
    case VMX_ERR_CHECKPOINT:   return "checkpoint error";
    case VMX_ERR_RETENTION:    return "retention error";
    case VMX_ERR_ROUTE:        return "route error";
    case VMX_ERR_BACKPRESSURE: return "backpressure error";
    case VMX_ERR_BUSY:         return "busy";
    case VMX_ERR_CLOSED:       return "closed";
    default:                   return "unknown error";
    }
}
