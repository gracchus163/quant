// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2016-2018, NetApp, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <warpcore/warpcore.h>

#include "conn.h" // IWYU pragma: keep
#include "quic.h"
#include "tls.h"


#define STRM_FL_INI_SRV 0x01
#define STRM_FL_DIR_UNI 0x02


#define STRM_STATE(k, v) k = v
#define STRM_STATES                                                            \
    STRM_STATE(strm_idle, 0), STRM_STATE(strm_open, 1),                        \
        STRM_STATE(strm_hcrm, 2), STRM_STATE(strm_hclo, 3),                    \
        STRM_STATE(strm_clsd, 4)

// Define stream states.
// \dotfile conn-states.dot "Connection state diagram."
typedef enum { STRM_STATES } strm_state_t;

extern const char * const strm_state_str[];


struct q_stream {
    splay_entry(q_stream) node;
    struct q_conn * c;

    struct w_iov_sq out;    ///< Tail queue containing outbound data.
    struct w_iov * out_una; ///< Lowest un-ACK'ed data chunk.
    struct w_iov * out_nxt; ///< Lowest unsent data chunk.
    uint64_t out_ack_cnt;   ///< Number of unique ACKs received for pkts in o.
    uint64_t out_data;      ///< Current outbound stream offset (= data sent).
    uint64_t out_data_max;  ///< Outbound max_stream_data.

    struct w_iov_sq in;       ///< Tail queue containing inbound data.
    struct ooo_by_off in_ooo; ///< Out-of-order inbound data.
    uint64_t in_data_max;     ///< Inbound max_stream_data.
    uint64_t new_in_data_max; ///< New inbound max_stream_data (for update).
    uint64_t in_data;         ///< In-order stream data received.

    int64_t id;
    strm_state_t state;
    uint8_t tx_max_stream_data : 1; ///< We need to open the receive window.
    uint8_t blocked : 1;            ///< We are receive-window-blocked.
    uint8_t tx_fin : 1;             ///< We need to send a FIN.
    uint8_t : 5;
    uint8_t _unused[3];
};


#ifndef NDEBUG
#define strm_to_state(s, new_state)                                            \
    do {                                                                       \
        if ((s)->id >= 0) {                                                    \
            warn(DBG, "conn %s strm " FMT_SID " state %s -> " YEL "%s" NRM,    \
                 (s)->c->scid ? cid2str((s)->c->scid) : "?", (s)->id,          \
                 strm_state_str[(s)->state], strm_state_str[(new_state)]);     \
        }                                                                      \
        (s)->state = (new_state);                                              \
    } while (0)
#else
#define strm_to_state(s, new_state) (s)->state = (new_state)
#endif


static inline bool __attribute__((nonnull, always_inline))
out_fully_acked(const struct q_stream * const s)
{
    return s->out_ack_cnt == sq_len(&s->out);
}


static inline int64_t __attribute__((always_inline, const))
crpt_strm_id(const epoch_t epoch)
{
    return -((int64_t)epoch + 1);
}


static inline epoch_t __attribute__((nonnull, always_inline))
strm_epoch(const struct q_stream * const s)
{
    if (s->id < 0)
        return (epoch_t)(-s->id) - 1;
    if (s->c->state == conn_opng)
        return 1;
    return 3;
}


static inline bool __attribute__((nonnull, always_inline))
stream_needs_ctrl(const struct q_stream * const s)
{
    return s->tx_fin || s->tx_max_stream_data;
}


static inline int __attribute__((nonnull, always_inline))
streams_by_id_cmp(const struct q_stream * const a,
                  const struct q_stream * const b)
{
    return (a->id > b->id) - (a->id < b->id);
}


SPLAY_PROTOTYPE(streams_by_id, q_stream, node, streams_by_id_cmp)


extern struct q_stream * __attribute__((nonnull))
get_stream(struct q_conn * const c, const int64_t id);

extern struct q_stream * new_stream(struct q_conn * const c, const int64_t id);

extern void __attribute__((nonnull)) free_stream(struct q_stream * const s);

extern void __attribute__((nonnull))
track_bytes_in(struct q_stream * const s, const uint64_t n);

extern void __attribute__((nonnull))
track_bytes_out(struct q_stream * const s, const uint64_t n);

extern void __attribute__((nonnull))
reset_stream(struct q_stream * const s, const bool forget);

extern void __attribute__((nonnull))
apply_stream_limits(struct q_stream * const s);

extern void __attribute__((nonnull)) do_stream_fc(struct q_stream * const s);

extern void __attribute__((nonnull)) do_stream_id_fc(struct q_stream * const s);

extern void __attribute__((nonnull))
concat_out(struct q_stream * const s, struct w_iov_sq * const q);
