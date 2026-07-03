/*
 * libvaultmux public API declarations.
 * This header provides the complete public API surface.
 */
#ifndef VMX_API_H
#define VMX_API_H

#include "vaultmux.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ═══════ Context & Session ═══════ */
vmx_context_t *vmx_context_create(void);
void           vmx_context_destroy(vmx_context_t *ctx);
vmx_session_t *vmx_session_create(vmx_context_t *ctx, uint32_t capabilities);
void           vmx_session_destroy(vmx_session_t *sess);
uint32_t       vmx_session_get_id(const vmx_session_t *sess);
uint32_t       vmx_session_get_epoch(const vmx_session_t *sess);
uint32_t       vmx_session_get_capabilities(const vmx_session_t *sess);

/* ═══════ Protocol ═══════ */
int vmx_protocol_parse_header(const uint8_t *data, size_t len,
                              vmx_parsed_header_t *out);
int vmx_protocol_parse_frame(const uint8_t *data, size_t len,
                             vmx_parsed_frame_t *out);
int vmx_ingest_frames(vmx_session_t *sess, const uint8_t *data, size_t len);
int vmx_frame_dispatch(vmx_session_t *sess, const vmx_parsed_frame_t *f);

/* ═══════ Span ═══════ */
vmx_span_t *vmx_span_registry_create(vmx_session_t *sess, uint32_t span_id,
                                     uint32_t owner_token);
vmx_span_t *vmx_span_registry_lookup(vmx_session_t *sess, uint32_t span_id);
int         vmx_span_registry_destroy(vmx_session_t *sess, vmx_span_t *span);
int         vmx_span_lifecycle_set_state(vmx_span_t *span,
                                         vmx_lifecycle_state_t state);
int         vmx_span_lifecycle_incref(vmx_span_t *span);
int         vmx_span_lifecycle_decref(vmx_session_t *sess, vmx_span_t *span);
int         vmx_span_lifecycle_deferred_destroy(vmx_session_t *sess,
                                                vmx_span_t *span);

/* ═══════ Segment ═══════ */
vmx_segment_t *vmx_segment_alloc(vmx_session_t *sess, uint32_t span_id,
                                 size_t capacity);
int            vmx_segment_write(vmx_segment_t *seg, const uint8_t *data,
                                 size_t len);
int            vmx_segment_realloc(vmx_session_t *sess, vmx_segment_t *seg,
                                   size_t new_capacity);
int            vmx_segment_compact(vmx_session_t *sess, vmx_span_t *span);
int            vmx_segment_gc_reclaim(vmx_session_t *sess, vmx_segment_t *seg);
int            vmx_segment_gc_span(vmx_session_t *sess, vmx_span_t *span);

/* ═══════ Lease ═══════ */
vmx_lease_t *vmx_lease_acquire(vmx_session_t *sess, uint32_t span_id,
                               uint32_t owner_token);
int          vmx_lease_release(vmx_session_t *sess, vmx_lease_t *lease);
int          vmx_lease_force_release(vmx_session_t *sess, vmx_lease_t *lease);
int          vmx_lease_renew(vmx_session_t *sess, vmx_lease_t *lease);
vmx_lease_t *vmx_lease_lookup(vmx_session_t *sess, uint32_t lease_id);
vmx_lease_t *vmx_lease_find_by_span(vmx_session_t *sess, uint32_t span_id);

/* ═══════ Cursor ═══════ */
vmx_cursor_t *vmx_cursor_open(vmx_session_t *sess, uint32_t span_id,
                              uint32_t lease_id);
int           vmx_cursor_close(vmx_session_t *sess, vmx_cursor_t *cursor);
int           vmx_cursor_advance_to_segment(vmx_session_t *sess,
                                            vmx_cursor_t *cursor,
                                            uint32_t segment_index);
int           vmx_cursor_materialize(vmx_session_t *sess,
                                     vmx_cursor_t *cursor,
                                     uint8_t *out, size_t out_len,
                                     size_t *bytes_read);
vmx_cursor_t *vmx_cursor_lookup(vmx_session_t *sess, uint32_t cursor_id);
int           vmx_cursor_invalidate_for_span(vmx_session_t *sess,
                                             uint32_t span_id);

/* ═══════ Channel ═══════ */
vmx_channel_t *vmx_channel_mux_alloc(vmx_session_t *sess, uint32_t channel_id);
vmx_channel_t *vmx_channel_lookup(vmx_session_t *sess, uint32_t channel_id);
int            vmx_channel_bind_span(vmx_session_t *sess, vmx_channel_t *ch,
                                     uint32_t span_id, uint32_t partition_id);
int            vmx_channel_dispatch_to_span(vmx_session_t *sess,
                                            vmx_channel_t *ch,
                                            const uint8_t *data, size_t len);
void           vmx_session_gc_drained_channels(vmx_session_t *sess);

/* ═══════ Stream ═══════ */
int vmx_stream_open(vmx_session_t *sess, uint32_t channel_id,
                    uint32_t stream_id);
int vmx_stream_close(vmx_session_t *sess, vmx_stream_t *stream);
int vmx_stream_buffer_append(vmx_session_t *sess, vmx_stream_t *stream,
                             const uint8_t *data, size_t len);
int vmx_stream_flush_pending(vmx_session_t *sess, vmx_stream_t *stream);
vmx_stream_t *vmx_stream_lookup(vmx_session_t *sess, uint32_t stream_id);

/* ═══════ Replication ═══════ */
int vmx_repl_setup(vmx_session_t *sess, uint32_t span_id, uint8_t quorum);
int vmx_repl_add_member(vmx_session_t *sess, uint32_t rs_id, uint32_t node_id);
int vmx_repl_remove_member(vmx_session_t *sess, uint32_t rs_id, uint32_t node_id);
int vmx_repl_check_quorum(vmx_session_t *sess, vmx_replica_set_t *rs);
int vmx_repl_snapshot_segments(vmx_session_t *sess, vmx_replica_set_t *rs);
vmx_replica_set_t *vmx_repl_lookup(vmx_session_t *sess, uint32_t rs_id);
vmx_replica_set_t *vmx_repl_find_by_span(vmx_session_t *sess, uint32_t span_id);

/* ═══════ Migration ═══════ */
int vmx_migrate_initiate(vmx_session_t *sess, uint32_t span_id,
                         uint32_t target_node);
int vmx_migrate_abort(vmx_session_t *sess, vmx_migration_t *mig);
int vmx_migrate_prepare_source(vmx_session_t *sess, vmx_migration_t *mig);
int vmx_migrate_complete_handoff(vmx_session_t *sess, vmx_migration_t *mig);
vmx_migration_t *vmx_migrate_lookup(vmx_session_t *sess, uint32_t id);
vmx_migration_t *vmx_migrate_find_by_span(vmx_session_t *sess, uint32_t span_id);

/* ═══════ Route ═══════ */
int vmx_route_advertise(vmx_session_t *sess, uint32_t span_id,
                        uint32_t partition_id, uint32_t node_id);
int vmx_route_add_node(vmx_session_t *sess, uint32_t node_id,
                       const char *addr, uint16_t port);
int vmx_route_remove_node(vmx_session_t *sess, uint32_t node_id);
vmx_route_entry_t *vmx_route_lookup(vmx_session_t *sess, uint32_t route_id);
vmx_node_desc_t   *vmx_node_lookup(vmx_session_t *sess, uint32_t node_id);

/* ═══════ Partition ═══════ */
vmx_partition_t *vmx_partition_lookup(vmx_session_t *sess, uint32_t pid);
int vmx_partition_add(vmx_session_t *sess, uint32_t pid,
                      uint32_t max_chunk, uint32_t window);
int vmx_partition_remove(vmx_session_t *sess, uint32_t pid);

/* ═══════ Backpressure ═══════ */
int vmx_backpressure_add_partition(vmx_session_t *sess, uint32_t pid,
                                  int32_t credits, int32_t max_credits);
int vmx_backpressure_remove_partition(vmx_session_t *sess, uint32_t pid);
int vmx_backpressure_apply_credits(vmx_session_t *sess, uint32_t pid,
                                   int32_t delta);

/* ═══════ Checkpoint ═══════ */
int vmx_checkpoint_begin(vmx_session_t *sess, uint32_t span_id,
                         uint32_t ckpt_id);
int vmx_checkpoint_flush(vmx_session_t *sess, vmx_checkpoint_t *ckpt);
int vmx_checkpoint_restore(vmx_session_t *sess, vmx_checkpoint_t *ckpt);
int vmx_checkpoint_cleanup_listener(vmx_session_t *sess,
                                    vmx_checkpoint_t *ckpt);
vmx_checkpoint_t *vmx_checkpoint_lookup(vmx_session_t *sess, uint32_t id);

/* ═══════ Utilities ═══════ */
uint32_t vmx_crc32c(const uint8_t *data, size_t len);
uint32_t vmx_fnv1a(const uint8_t *data, size_t len);
uint32_t vmx_hash_mix(uint32_t a, uint32_t b);
uint32_t vmx_next_power_of_2(uint32_t v);
bool     vmx_is_power_of_2(uint32_t v);

void     vmx_write_u16(uint8_t *buf, uint16_t val);
void     vmx_write_u32(uint8_t *buf, uint32_t val);
void     vmx_write_u64(uint8_t *buf, uint64_t val);
uint16_t vmx_read_u16(const uint8_t *buf);
uint32_t vmx_read_u32(const uint8_t *buf);
uint64_t vmx_read_u64(const uint8_t *buf);

#endif /* VMX_API_H */

/*
 * Channel multiplexing functions.
 *
 * Channels provide logical multiplexing within a session,
 * allowing concurrent operations on different spans through
 * a single session. Each channel is bound to a span and
 * partition for data operations.
 *
 * Channel lifecycle:
 *   open   - allocate channel with unique ID
 *   bind   - associate with span and partition
 *   remap  - change partition binding
 *   drain  - stop accepting new operations
 *   close  - release channel resources
 *
 * Channels support backpressure through credit-based flow
 * control. Each channel's partition has a credit balance
 * that limits the rate of operations.
 */

/*
 * Stream functions.
 *
 * Streams provide ordered byte-stream abstraction over
 * channel-multiplexed segment access. Streams handle
 * fragment reassembly, buffered appends, and flushing.
 *
 * Stream operations:
 *   open     - create stream on a channel
 *   append   - buffer data for writing to span segments
 *   flush    - write buffered data to target segment
 *   read     - materialize data through cursor
 *   close    - release stream resources
 */

/*
 * Replication functions.
 *
 * Replication provides data durability through replica sets.
 * Each replica set maintains copies of span data across
 * multiple nodes with quorum-based consistency.
 *
 * Operations:
 *   setup    - create replica set for a span
 *   add/remove member - manage replica set membership
 *   quorum check - verify sufficient replicas are alive
 *   sync     - snapshot and transfer segment data
 *   health   - monitor replica health and detect split-brain
 */

/*
 * Migration functions.
 *
 * Migration orchestrates live span transfer between nodes
 * for cluster rebalancing. The migration protocol:
 *   1. initiate  - mark span for migration, assign target
 *   2. prepare   - reallocate segments with transfer headers
 *   3. transfer  - stream segment data to target node
 *   4. handoff   - transfer ownership, free source segments
 *   5. complete  - finalize migration, update routes
 *
 * Migrations can be aborted at any phase before handoff.
 */

/*
 * Checkpoint functions.
 *
 * Checkpoints provide consistent snapshots of span state
 * for durability and recovery. A checkpoint captures
 * segment pointers at a specific generation.
 *
 * Checkpoint lifecycle:
 *   begin    - snapshot current segment state
 *   flush    - serialize snapshotted segments
 *   restore  - recover segment state from checkpoint
 */

/*
 * Retention and backpressure functions.
 *
 * Retention manages automatic cleanup of old segment data
 * based on configurable policies (age, size thresholds).
 *
 * Backpressure provides credit-based flow control
 * per partition, using AIMD congestion control.
 */

/*
 * Route and partition functions.
 *
 * Routes map spans to nodes and partitions.
 * Partitions define data distribution boundaries
 * with configurable chunk sizes and flow control windows.
 *
 * The route table supports consistent hashing for
 * balanced partition-to-node mapping, with automatic
 * rebalancing when nodes join or depart the cluster.
 * Route metrics track per-route latency, throughput,
 * and error rates for intelligent routing decisions.
 */
