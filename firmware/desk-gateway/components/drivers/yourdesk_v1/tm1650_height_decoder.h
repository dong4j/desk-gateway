/**
 * @file tm1650_height_decoder.h
 * @brief Assemble TM1650 digit writes and decode the displayed desk height.
 *
 * The controller writes one segment byte to each of addresses 0x34-0x37.
 * This decoder is deliberately independent of ESP-IDF so captured frames can
 * be replayed in a host-side golden-vector check.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TM1650_HEIGHT_WAITING = 0,
    TM1650_HEIGHT_VALID,
    TM1650_HEIGHT_INVALID,
} tm1650_height_result_t;

typedef struct {
    uint8_t digits[4];
    uint8_t received_mask;
    uint8_t next_order_index;
} tm1650_height_decoder_t;

/** Timestamped numeric-digit cache used only after a full height is known. */
typedef struct {
    uint8_t digits[3];
    uint32_t seen_ms[3];
    uint8_t received_mask;
} tm1650_height_cache_t;

/**
 * Confirmed and pending numeric registers matching the original TM1650 display.
 *
 * Individual controller writes are not atomic: during a decimal carry the bus
 * can briefly expose a valid-looking mixture of the old and new display.  The
 * pending copy therefore remains private until the caller observes a quiet
 * settle window and accepts the reconstructed height.
 */
typedef struct {
    uint8_t committed_digits[3];
    uint8_t pending_digits[3];
    uint32_t last_write_ms;
    uint8_t initialized;
    uint8_t pending_dirty;
} tm1650_height_registers_t;

/** Reset an incomplete display frame. */
void tm1650_height_decoder_reset(tm1650_height_decoder_t *decoder);

/**
 * Feed one completed digit write.
 *
 * A new DIG3 (0x36) starts the observed controller write order
 * DIG3 -> DIG2 -> DIG1 -> DIG4. A height is returned as soon as the three
 * numeric digits arrive exactly once in order. DIG4 is an optional mirror that
 * may lag or interleave a later numeric refresh, so it is ignored for assembly;
 * other fragments must still not be combined across display refreshes.
 */
tm1650_height_result_t tm1650_height_decoder_feed(tm1650_height_decoder_t *decoder,
                                                   uint8_t addr7,
                                                   uint8_t segment,
                                                   int *out_height_mm);

/** Reset all timestamped digit observations. */
void tm1650_height_cache_reset(tm1650_height_cache_t *cache);

/**
 * Feed one numeric digit into a rolling cache.
 *
 * Upward motor noise can split one display refresh into otherwise valid digit
 * writes hundreds of milliseconds apart. Once the caller already owns a full
 * baseline, this cache can reconstruct a candidate from DIG1/2/3 observations
 * no older than max_age_ms. The caller must still apply direction and speed
 * validation before publishing it. out_oldest_age_ms lets safety prediction
 * account for the oldest component instead of treating a mixed candidate as a
 * brand-new measurement.
 */
tm1650_height_result_t tm1650_height_cache_feed(
    tm1650_height_cache_t *cache, uint8_t addr7, uint8_t segment,
    uint32_t now_ms, uint32_t max_age_ms, int *out_height_mm,
    uint32_t *out_oldest_age_ms);

/** Reset the persistent numeric-register mirror to an untrusted state. */
void tm1650_height_registers_reset(tm1650_height_registers_t *registers);

/**
 * Establish the register mirror from one complete, accepted controller frame.
 *
 * A strict frame remains the trust boundary. After this seed, individual
 * numeric writes can be staged exactly as the original TM1650 display keeps
 * untouched registers latched.
 */
tm1650_height_result_t tm1650_height_registers_seed(
    tm1650_height_registers_t *registers, const uint8_t digits[3],
    int *out_height_mm);

/**
 * Stage one numeric write in the pending display-register mirror.
 *
 * This deliberately does not decode or publish immediately. The physical
 * controller can update multiple digits tens of milliseconds apart, so the
 * caller must wait for a quiet interval and call
 * tm1650_height_registers_settle().
 */
tm1650_height_result_t tm1650_height_registers_feed(
    tm1650_height_registers_t *registers, uint8_t addr7, uint8_t segment,
    uint32_t now_ms);

/** Return non-zero when staged digits still need a settle decision. */
uint8_t tm1650_height_registers_has_pending(
    const tm1650_height_registers_t *registers);

/**
 * Decode the pending mirror after no numeric write arrived for settle_ms.
 *
 * A returned candidate is still untrusted. The caller must apply direction and
 * speed validation, then explicitly commit or discard it.
 */
tm1650_height_result_t tm1650_height_registers_settle(
    tm1650_height_registers_t *registers, uint32_t now_ms,
    uint32_t settle_ms, int *out_height_mm);

/** Accept the current pending registers as the next confirmed baseline. */
void tm1650_height_registers_commit(tm1650_height_registers_t *registers);

/** Roll pending registers back to the last confirmed baseline. */
void tm1650_height_registers_discard(tm1650_height_registers_t *registers);

#ifdef __cplusplus
}
#endif
