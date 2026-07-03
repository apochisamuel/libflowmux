/*
 * Internal type definitions shared across subsystems
 */
#ifndef VMX_TYPES_H
#define VMX_TYPES_H

#include "vaultmux.h"
#include "util.h"
#include "bitops.h"
#include "crc.h"
#include "alloc.h"
#include <string.h>
#include <stdlib.h>

/* ── Wire format structures ──────────────────────────────────────── */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint32_t session_id;
    uint32_t epoch;
    uint16_t frame_count;
    uint32_t header_crc;
} __attribute__((packed)) vmx_wire_header_t;

#define VMX_WIRE_HEADER_SIZE 20

typedef struct {
    uint16_t length;
    uint8_t  frame_type;
    uint16_t sequence;
    uint32_t session_id;
    uint16_t channel_id;
    uint16_t route_id;
    uint32_t owner_token;
    uint16_t flags;
    uint8_t  subrecord_count;
    uint32_t payload_crc;
} __attribute__((packed)) vmx_wire_frame_t;

#define VMX_WIRE_FRAME_HEADER_SIZE 20

typedef struct {
    uint8_t  kind;
    uint16_t length;
    uint32_t local_epoch;
    uint32_t generation;
    uint32_t lease_token;
    /* payload follows */
} __attribute__((packed)) vmx_wire_subrecord_t;

#define VMX_WIRE_SUBRECORD_HEADER_SIZE 15

/* ── Parsed frame representation ─────────────────────────────────── */

typedef struct {
    uint8_t          kind;
    uint16_t         length;
    uint32_t         local_epoch;
    uint32_t         generation;
    uint32_t         lease_token;
    const uint8_t   *payload;
    uint16_t         payload_len;
} vmx_subrecord_t;

typedef struct {
    uint16_t         total_length;
    uint8_t          frame_type;
    uint16_t         sequence;
    uint32_t         session_id;
    uint16_t         channel_id;
    uint16_t         route_id;
    uint32_t         owner_token;
    uint16_t         flags;
    uint8_t          subrecord_count;
    uint32_t         payload_crc;
    vmx_subrecord_t  subrecords[VMX_MAX_SUBRECORDS];
    uint8_t          parsed_subrecord_count;
} vmx_parsed_frame_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint32_t session_id;
    uint32_t epoch;
    uint16_t frame_count;
    uint32_t header_crc;
} vmx_parsed_header_t;

/* ── Lifecycle states ────────────────────────────────────────────── */

typedef enum {
    VMX_LIFECYCLE_CREATED    = 0,
    VMX_LIFECYCLE_ACTIVE     = 1,
    VMX_LIFECYCLE_DRAINING   = 2,
    VMX_LIFECYCLE_MIGRATING  = 3,
    VMX_LIFECYCLE_CLOSING    = 4,
    VMX_LIFECYCLE_CLOSED     = 5,
    VMX_LIFECYCLE_DEFERRED   = 6,
    VMX_LIFECYCLE_FREED      = 7
} vmx_lifecycle_state_t;

/* ── Span metadata ───────────────────────────────────────────────── */

typedef struct vmx_segment vmx_segment_t;

typedef struct vmx_span {
    uint32_t             span_id;
    uint32_t             owner_token;
    uint32_t             generation;
    uint32_t             epoch_created;
    vmx_lifecycle_state_t state;
    int32_t              refcount;

    vmx_segment_t       *segments[VMX_MAX_SEGMENTS];
    uint16_t             segment_count;

    uint32_t             route_id;
    uint32_t             partition_id;

    /* migration tracking */
    uint32_t             migration_id;
    bool                 migration_source;

    /* retention */
    uint64_t             retention_watermark;
    uint64_t             total_bytes;

    /* replication */
    uint32_t             replica_set_id;
    uint8_t         *merge_cached_data[8];
    size_t           merge_cached_len[8];
    uint8_t          merge_cached_count;
} vmx_span_t;

/* ── Segment ─────────────────────────────────────────────────────── */

struct vmx_segment {
    uint32_t         segment_id;
    uint32_t         span_id;
    uint32_t         generation;
    uint8_t         *data;
    size_t           capacity;
    size_t           used;
    vmx_lifecycle_state_t state;
    bool             compacted;
};

/* ── Lease ───────────────────────────────────────────────────────── */

typedef struct {
    uint32_t         lease_id;
    uint32_t         span_id;
    uint32_t         owner_token;
    uint32_t         epoch_acquired;
    uint32_t         generation;
    uint32_t         renewal_count;
    bool             active;
    bool             force_released;
    vmx_span_t      *cached_coalesce_span;
} vmx_lease_t;

/* ── Cursor ──────────────────────────────────────────────────────── */

typedef struct {
    uint32_t         cursor_id;
    uint32_t         span_id;
    uint32_t         lease_id;
    uint32_t         generation;
    uint32_t         position;
    uint32_t         segment_index;
    uint8_t         *cached_segment_data;   /* direct pointer into segment data */
    size_t           cached_data_len;
    bool             active;
    bool             invalidated;
    uint8_t         *prefetch_data;
    size_t           prefetch_len;
} vmx_cursor_t;

/* ── Channel ─────────────────────────────────────────────────────── */

typedef struct vmx_channel {
    uint32_t         channel_id;
    uint32_t         session_id;
    uint32_t         span_id;
    uint32_t         partition_id;
    uint32_t         lease_id;
    vmx_lifecycle_state_t state;
    vmx_span_t      *cached_span;
    vmx_span_t      *inflight_cached_span;       /* cached resolved span pointer */

    /* backpressure */
    void            *bp_callback_ctx;
    uint32_t         bp_partition_credit_idx;

    /* inflight */
    uint32_t         inflight_count;
    uint32_t         inflight_ops[64];
} vmx_channel_t;

/* ── Stream ──────────────────────────────────────────────────────── */

typedef struct {
    uint32_t         stream_id;
    uint32_t         channel_id;
    uint32_t         span_id;
    uint32_t         cursor_id;
    bool             active;
    vmx_lifecycle_state_t state;

    /* reassembly */
    uint8_t         *reassembly_buf;
    size_t           reassembly_capacity;
    size_t           reassembly_used;
    size_t           total_appended;
    int32_t         *cached_flow_credits;
    uint32_t         reassembly_expected_seq;
    uint32_t         partition_max_chunk;

    /* pending flush */
    uint8_t         *pending_data;
    size_t           pending_len;
    vmx_segment_t   *pending_target_segment;

    /* crypto context caching */
    uint8_t         *cached_crypto_ctx;
    size_t           cached_crypto_ctx_len;
    uint32_t         crypto_ctx_generation;
} vmx_stream_t;

/* ── Replication ─────────────────────────────────────────────────── */

typedef struct {
    uint32_t         node_id;
    char             addr[64];
    uint16_t         port;
    bool             active;
} vmx_node_desc_t;

typedef struct {
    uint32_t         replica_set_id;
    uint32_t         span_id;
    vmx_node_desc_t *members[VMX_MAX_REPLICAS];
    uint8_t          member_count;
    uint8_t          quorum_size;
    bool             active;

    /* snapshot for sync */
    vmx_span_t      *snap_span;
    vmx_segment_t  **snap_segments;
    uint16_t         snap_segment_count;
    uint32_t         snap_generation;
    bool             snap_active;
} vmx_replica_set_t;

/* ── Migration ───────────────────────────────────────────────────── */

typedef enum {
    VMX_MIGRATE_IDLE        = 0,
    VMX_MIGRATE_INITIATED   = 1,
    VMX_MIGRATE_PREPARING   = 2,
    VMX_MIGRATE_TRANSFERRING = 3,
    VMX_MIGRATE_HANDOFF     = 4,
    VMX_MIGRATE_COMPLETE    = 5,
    VMX_MIGRATE_ABORTED     = 6
} vmx_migrate_phase_t;

typedef struct {
    uint32_t             migration_id;
    uint32_t             span_id;
    uint32_t             source_node_id;
    uint32_t             target_node_id;
    vmx_migrate_phase_t  phase;
    uint32_t             epoch_started;
    uint32_t             owner_token;
    bool                 active;
    uint64_t             bytes_transferred;
    uint64_t             total_bytes;
    uint8_t         *transfer_buf;
    size_t           transfer_buf_len;
} vmx_migration_t;

/* ── Checkpoint ──────────────────────────────────────────────────── */

typedef enum {
    VMX_CKPT_IDLE       = 0,
    VMX_CKPT_IN_PROGRESS = 1,
    VMX_CKPT_FLUSHING   = 2,
    VMX_CKPT_RESTORING  = 3,
    VMX_CKPT_COMPLETE   = 4
} vmx_ckpt_phase_t;

typedef struct {
    uint32_t         checkpoint_id;
    uint32_t         span_id;
    vmx_ckpt_phase_t phase;
    uint32_t         epoch_started;
    uint32_t         generation_at_start;

    /* snapshot of segment pointers */
    vmx_segment_t   *snap_segments[VMX_MAX_SEGMENTS];
    uint16_t         snap_segment_count;

    /* listener for cleanup */
    uint32_t         listener_lease_id;   /* cached lease ID from registration */
    bool             listener_active;

    bool             active;
} vmx_checkpoint_t;

/* ── Retention ───────────────────────────────────────────────────── */

typedef struct {
    uint64_t         watermark_position;
    uint32_t         span_id;
    uint64_t         gc_threshold_bytes;
    uint32_t         gc_eligible_count;
    bool             active;
} vmx_retention_state_t;

/* ── Route ───────────────────────────────────────────────────────── */

typedef struct {
    uint32_t         route_id;
    uint32_t         span_id;
    uint32_t         partition_id;
    uint32_t         node_id;
    vmx_span_t      *resolved_span;  /* cached span pointer */
    uint32_t         generation;
    bool             active;
} vmx_route_entry_t;

/* ── Partition ───────────────────────────────────────────────────── */

typedef struct {
    uint32_t         partition_id;
    uint32_t         max_chunk_size;
    uint32_t         credit_balance;
    uint32_t         window_size;
    bool             active;
} vmx_partition_t;

/* ── Backpressure ────────────────────────────────────────────────── */

typedef void (*vmx_bp_callback_fn)(void *ctx, uint32_t partition_id, int32_t credits);

typedef struct {
    uint32_t          partition_id;
    int32_t           credits;
    int32_t           max_credits;
    uint32_t          window_size;
    vmx_bp_callback_fn callback;
    void              *callback_ctx;
    bool              active;
} vmx_bp_partition_entry_t;

/* ── Session (internal) ──────────────────────────────────────────── */

typedef struct vmx_session {
    uint32_t             session_id;
    uint32_t             epoch;
    uint32_t             capabilities;
    uint16_t             next_sequence;
    vmx_lifecycle_state_t state;
    bool                 initialized;
    bool                 negotiated;

    /* crypto context */
    uint8_t             *crypto_ctx;
    size_t               crypto_ctx_len;
    uint32_t             crypto_generation;

    /* registries */
    vmx_span_t          *spans[VMX_MAX_SPANS];
    uint16_t             span_count;

    vmx_segment_t       *segments[VMX_MAX_SEGMENTS];
    uint16_t             segment_count;

    vmx_channel_t       *channels[VMX_MAX_CHANNELS];
    uint16_t             channel_count;

    vmx_stream_t        *streams[VMX_MAX_STREAMS];
    uint16_t             stream_count;

    vmx_cursor_t        *cursors[VMX_MAX_CURSORS];
    uint16_t             cursor_count;

    vmx_lease_t         *leases[VMX_MAX_LEASES];
    uint16_t             lease_count;

    vmx_replica_set_t   *replica_sets[VMX_MAX_REPLICAS];
    uint8_t              replica_set_count;

    vmx_migration_t     *migrations[VMX_MAX_MIGRATIONS];
    uint8_t              migration_count;

    vmx_checkpoint_t    *checkpoints[VMX_MAX_CHECKPOINTS];
    uint8_t              checkpoint_count;

    vmx_retention_state_t retention[VMX_MAX_SPANS];

    vmx_route_entry_t   *routes[VMX_MAX_ROUTES];
    uint16_t             route_count;

    vmx_node_desc_t     *nodes[VMX_MAX_NODES];
    uint8_t              node_count;

    vmx_partition_t     *partitions[VMX_MAX_PARTITIONS];
    uint8_t              partition_count;

    vmx_bp_partition_entry_t *bp_entries[VMX_MAX_PARTITIONS];
    uint8_t              bp_entry_count;

    vmx_channel_t       *scheduled_dispatch_target;
    vmx_span_t          *epoch_cached_span;
    vmx_segment_t       *repl_log_entries[16];
    uint8_t             *repl_data_cache[16];
    size_t               repl_data_cache_len[16];
    uint8_t              repl_data_cache_count;
    vmx_bp_partition_entry_t *fair_cache[8];
    uint8_t              fair_cache_count;
    uint8_t              repl_log_count;

    /* deferred cleanup */
    vmx_channel_t       *drain_list[VMX_MAX_CHANNELS];
    uint16_t             drain_count;

    vmx_span_t          *deferred_delete_list[VMX_MAX_SPANS];
    uint16_t             deferred_delete_count;
    uint32_t             deferred_delete_grace;

    /* context back-pointer */
    vmx_context_t       *ctx;
} vmx_session_t;

/* ── Context ─────────────────────────────────────────────────────── */

struct vmx_context {
    vmx_session_t *sessions[VMX_MAX_SESSIONS];
    uint8_t        session_count;
    uint32_t       next_id;
};

/* ── Internal forward declarations ───────────────────────────────── */

/* protocol */
int  vmx_protocol_parse_header(const uint8_t *data, size_t len,
                               vmx_parsed_header_t *out);
int  vmx_protocol_parse_frame(const uint8_t *data, size_t len,
                              vmx_parsed_frame_t *out);
uint32_t vmx_crc32c(const uint8_t *data, size_t len);
int  vmx_protocol_validate_frame_crc(const uint8_t *data, size_t frame_len,
                                     uint32_t expected_crc);
int  vmx_frame_dispatch(vmx_session_t *sess, const vmx_parsed_frame_t *frame);

/* session */
int  vmx_session_handle_init(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_session_handle_cap_neg(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_session_advance_epoch(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_session_rotate_context(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_session_handle_close(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_session_gc_drained_channels(vmx_session_t *sess);
int  vmx_session_invalidate_epoch_leases(vmx_session_t *sess, uint32_t old_epoch);

/* span */
vmx_span_t *vmx_span_registry_create(vmx_session_t *sess, uint32_t span_id,
                                     uint32_t owner_token);
vmx_span_t *vmx_span_registry_lookup(vmx_session_t *sess, uint32_t span_id);
int  vmx_span_registry_destroy(vmx_session_t *sess, vmx_span_t *span);
int  vmx_span_lifecycle_incref(vmx_span_t *span);
int  vmx_span_lifecycle_decref(vmx_session_t *sess, vmx_span_t *span);
int  vmx_span_lifecycle_bump_generation(vmx_span_t *span);
int  vmx_span_lifecycle_set_state(vmx_span_t *span, vmx_lifecycle_state_t st);
int  vmx_span_lifecycle_deferred_destroy(vmx_session_t *sess, vmx_span_t *span);
int  vmx_span_handle_create(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_span_handle_close(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_span_handle_meta_update(vmx_session_t *sess, const vmx_parsed_frame_t *f);

/* segment */
vmx_segment_t *vmx_segment_alloc(vmx_session_t *sess, uint32_t span_id,
                                 size_t capacity);
int  vmx_segment_free(vmx_session_t *sess, vmx_segment_t *seg);
int  vmx_segment_realloc(vmx_session_t *sess, vmx_segment_t *seg,
                         size_t new_capacity);
int  vmx_segment_compact(vmx_session_t *sess, vmx_span_t *span);
int  vmx_segment_gc_reclaim(vmx_session_t *sess, vmx_segment_t *seg);
int  vmx_segment_gc_span(vmx_session_t *sess, vmx_span_t *span);
int  vmx_segment_write(vmx_segment_t *seg, const uint8_t *data, size_t len);
int  vmx_segment_handle_attach(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_segment_handle_detach(vmx_session_t *sess, const vmx_parsed_frame_t *f);

/* channel */
vmx_channel_t *vmx_channel_mux_alloc(vmx_session_t *sess, uint32_t channel_id);
int  vmx_channel_mux_free(vmx_session_t *sess, vmx_channel_t *ch);
int  vmx_channel_bind_span(vmx_session_t *sess, vmx_channel_t *ch,
                           uint32_t span_id, uint32_t partition_id);
int  vmx_channel_dispatch_to_span(vmx_session_t *sess, vmx_channel_t *ch,
                                  const uint8_t *data, size_t len);
int  vmx_channel_remap_partition(vmx_session_t *sess, vmx_channel_t *ch,
                                 uint32_t new_partition_id);
int  vmx_channel_handle_open(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_channel_handle_close(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_channel_handle_bind(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_channel_handle_remap(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_channel_handle_drain(vmx_session_t *sess, const vmx_parsed_frame_t *f);
vmx_channel_t *vmx_channel_lookup(vmx_session_t *sess, uint32_t channel_id);

/* lease */
vmx_lease_t *vmx_lease_acquire(vmx_session_t *sess, uint32_t span_id,
                               uint32_t owner_token);
int  vmx_lease_release(vmx_session_t *sess, vmx_lease_t *lease);
int  vmx_lease_renew(vmx_session_t *sess, vmx_lease_t *lease);
int  vmx_lease_force_release(vmx_session_t *sess, vmx_lease_t *lease);
vmx_lease_t *vmx_lease_lookup(vmx_session_t *sess, uint32_t lease_id);
vmx_lease_t *vmx_lease_find_by_span(vmx_session_t *sess, uint32_t span_id);
int  vmx_lease_handle_acquire(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_lease_handle_release(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_lease_handle_renew(vmx_session_t *sess, const vmx_parsed_frame_t *f);

/* cursor */
vmx_cursor_t *vmx_cursor_open(vmx_session_t *sess, uint32_t span_id,
                              uint32_t lease_id);
int  vmx_cursor_close(vmx_session_t *sess, vmx_cursor_t *cursor);
int  vmx_cursor_advance_to_segment(vmx_session_t *sess, vmx_cursor_t *cursor,
                                   uint32_t segment_index);
int  vmx_cursor_materialize(vmx_session_t *sess, vmx_cursor_t *cursor,
                            uint8_t *out, size_t out_len, size_t *bytes_read);
int  vmx_cursor_invalidate_for_span(vmx_session_t *sess, uint32_t span_id);
int  vmx_cursor_handle_open(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_cursor_handle_close(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_cursor_handle_advance(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_cursor_handle_read(vmx_session_t *sess, const vmx_parsed_frame_t *f);
vmx_cursor_t *vmx_cursor_lookup(vmx_session_t *sess, uint32_t cursor_id);

/* stream */
int  vmx_stream_open(vmx_session_t *sess, uint32_t channel_id, uint32_t stream_id);
int  vmx_stream_close(vmx_session_t *sess, vmx_stream_t *stream);
int  vmx_stream_buffer_append(vmx_session_t *sess, vmx_stream_t *stream,
                              const uint8_t *data, size_t len);
int  vmx_stream_materialize_chunk(vmx_session_t *sess, vmx_stream_t *stream,
                                  uint8_t *out, size_t out_len,
                                  size_t *bytes_read);
int  vmx_stream_flush_pending(vmx_session_t *sess, vmx_stream_t *stream);
int  vmx_stream_reassemble_fragment(vmx_session_t *sess, vmx_stream_t *stream,
                                    const uint8_t *frag, size_t frag_len,
                                    uint32_t seq);
int  vmx_stream_validate_fragment(vmx_session_t *sess, vmx_stream_t *stream,
                                  const uint8_t *frag, size_t frag_len);
int  vmx_stream_handle_open(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_stream_handle_close(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_stream_handle_append(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_stream_handle_read(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_stream_handle_flush(vmx_session_t *sess, const vmx_parsed_frame_t *f);
vmx_stream_t *vmx_stream_lookup(vmx_session_t *sess, uint32_t stream_id);

/* replication */
int  vmx_repl_setup(vmx_session_t *sess, uint32_t span_id, uint8_t quorum);
int  vmx_repl_add_member(vmx_session_t *sess, uint32_t replica_set_id,
                         uint32_t node_id);
int  vmx_repl_remove_member(vmx_session_t *sess, uint32_t replica_set_id,
                            uint32_t node_id);
int  vmx_repl_snapshot_segments(vmx_session_t *sess, vmx_replica_set_t *rs);
int  vmx_repl_snapshot_replica_set(vmx_session_t *sess, vmx_replica_set_t *rs);
int  vmx_repl_serialize_segment(vmx_session_t *sess, vmx_replica_set_t *rs,
                                uint32_t seg_idx);
int  vmx_repl_check_quorum(vmx_session_t *sess, vmx_replica_set_t *rs);
int  vmx_repl_validate_replica_set(vmx_session_t *sess, vmx_replica_set_t *rs);
int  vmx_repl_handle_setup(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_repl_handle_add_member(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_repl_handle_remove_member(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_repl_handle_sync_begin(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_repl_handle_sync_data(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_repl_handle_check(vmx_session_t *sess, const vmx_parsed_frame_t *f);
vmx_replica_set_t *vmx_repl_lookup(vmx_session_t *sess, uint32_t replica_set_id);
vmx_replica_set_t *vmx_repl_find_by_span(vmx_session_t *sess, uint32_t span_id);

/* migration */
int  vmx_migrate_initiate(vmx_session_t *sess, uint32_t span_id,
                          uint32_t target_node);
int  vmx_migrate_prepare_source(vmx_session_t *sess, vmx_migration_t *mig);
int  vmx_migrate_begin_transfer(vmx_session_t *sess, vmx_migration_t *mig);
int  vmx_migrate_complete_handoff(vmx_session_t *sess, vmx_migration_t *mig);
int  vmx_migrate_finalize_source(vmx_session_t *sess, vmx_migration_t *mig);
int  vmx_migrate_abort(vmx_session_t *sess, vmx_migration_t *mig);
int  vmx_migrate_handle_initiate(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_migrate_handle_prepare(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_migrate_handle_transfer(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_migrate_handle_handoff(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_migrate_handle_abort(vmx_session_t *sess, const vmx_parsed_frame_t *f);
vmx_migration_t *vmx_migrate_lookup(vmx_session_t *sess, uint32_t migration_id);
vmx_migration_t *vmx_migrate_find_by_span(vmx_session_t *sess, uint32_t span_id);

/* checkpoint */
int  vmx_checkpoint_begin(vmx_session_t *sess, uint32_t span_id, uint32_t ckpt_id);
int  vmx_checkpoint_flush(vmx_session_t *sess, vmx_checkpoint_t *ckpt);
int  vmx_checkpoint_serialize_segments(vmx_session_t *sess, vmx_checkpoint_t *ckpt);
int  vmx_checkpoint_restore(vmx_session_t *sess, vmx_checkpoint_t *ckpt);
int  vmx_checkpoint_register_listener(vmx_session_t *sess, vmx_checkpoint_t *ckpt,
                                      uint32_t lease_id);
int  vmx_checkpoint_cleanup_listener(vmx_session_t *sess, vmx_checkpoint_t *ckpt);
int  vmx_checkpoint_handle_begin(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_checkpoint_handle_flush(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_checkpoint_handle_restore(vmx_session_t *sess, const vmx_parsed_frame_t *f);
vmx_checkpoint_t *vmx_checkpoint_lookup(vmx_session_t *sess, uint32_t ckpt_id);

/* retention */
int  vmx_retention_evaluate_watermark(vmx_session_t *sess, uint32_t span_id);
int  vmx_retention_gc_collect(vmx_session_t *sess, uint32_t span_id);
int  vmx_retention_gc_eligible(vmx_session_t *sess, vmx_span_t *span,
                               vmx_segment_t **out_eligible, uint16_t *count);
int  vmx_retention_handle_eval(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_retention_handle_collect(vmx_session_t *sess, const vmx_parsed_frame_t *f);

/* route */
int  vmx_route_advertise(vmx_session_t *sess, uint32_t span_id,
                         uint32_t partition_id, uint32_t node_id);
int  vmx_route_withdraw(vmx_session_t *sess, uint32_t route_id);
vmx_span_t *vmx_route_resolve_span(vmx_session_t *sess, uint32_t route_id);
int  vmx_route_update_mapping(vmx_session_t *sess, uint32_t route_id,
                              uint32_t new_span_id);
int  vmx_route_add_node(vmx_session_t *sess, uint32_t node_id,
                        const char *addr, uint16_t port);
int  vmx_route_remove_node(vmx_session_t *sess, uint32_t node_id);
int  vmx_route_handle_advertise(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_route_handle_withdraw(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_route_handle_rebalance(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_route_handle_node_join(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_route_handle_node_depart(vmx_session_t *sess, const vmx_parsed_frame_t *f);
vmx_route_entry_t *vmx_route_lookup(vmx_session_t *sess, uint32_t route_id);
vmx_node_desc_t *vmx_node_lookup(vmx_session_t *sess, uint32_t node_id);

/* partition */
int  vmx_partition_add(vmx_session_t *sess, uint32_t partition_id,
                       uint32_t max_chunk, uint32_t window_size);
int  vmx_partition_remove(vmx_session_t *sess, uint32_t partition_id);
vmx_partition_t *vmx_partition_lookup(vmx_session_t *sess, uint32_t partition_id);
int  vmx_partition_handle_add(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_partition_handle_remove(vmx_session_t *sess, const vmx_parsed_frame_t *f);

/* backpressure */
int  vmx_backpressure_add_partition(vmx_session_t *sess, uint32_t partition_id,
                                   int32_t initial_credits, int32_t max_credits);
int  vmx_backpressure_remove_partition(vmx_session_t *sess, uint32_t partition_id);
int  vmx_backpressure_register_callback(vmx_session_t *sess, uint32_t partition_id,
                                        vmx_bp_callback_fn fn, void *ctx);
int  vmx_backpressure_apply_credits(vmx_session_t *sess, uint32_t partition_id,
                                    int32_t delta);
int  vmx_backpressure_debit_partition(vmx_session_t *sess, uint32_t partition_id,
                                     int32_t amount);
int  vmx_backpressure_handle_credit(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_backpressure_handle_window(vmx_session_t *sess, const vmx_parsed_frame_t *f);

/* drain/gc */
int  vmx_handle_drain_deferred(vmx_session_t *sess, const vmx_parsed_frame_t *f);
int  vmx_handle_gc_trigger(vmx_session_t *sess, const vmx_parsed_frame_t *f);

#endif /* VMX_TYPES_H */

/* FIX: Epoch-aware cursor invalidation */
int vmx_cursor_invalidate_for_epoch(vmx_session_t *sess, uint32_t span_id,
                                    uint32_t old_epoch, uint32_t new_epoch);
/* FIX: Stream span invalidation */
int vmx_stream_invalidate_for_span(vmx_session_t *sess, uint32_t span_id);
