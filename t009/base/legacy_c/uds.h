/*
 * uds.h -- UDS (ISO 14229) diagnostic session handler, trailer brake ECU.
 *
 * Reference implementation. C99, freestanding-friendly:
 *   - no dynamic memory, no system clock, no I/O in the core logic,
 *   - all state lives in a caller-owned uds_ctx_t,
 *   - all buffers are provided by the caller and are bounds checked.
 *
 * TIME INJECTION
 * --------------
 * The handler never reads a clock. The caller passes a free-running
 * millisecond timestamp into every call:
 *
 *     uds_handle_request(&ctx, now_ms, req, req_len, resp, sizeof resp, &resp_len);
 *
 * `now_ms` must be monotonically non-decreasing. Wrap-around of the 32-bit
 * counter is handled by using unsigned difference arithmetic everywhere.
 * Timers (S3 session timeout, security lockout) are evaluated at the *start*
 * of each call, before the request is dispatched.
 */
#ifndef UDS_H
#define UDS_H

#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------- limits -- */

#define UDS_MAX_REQUEST_LEN    64u   /* longer request  -> NRC 0x13           */
#define UDS_MAX_RESPONSE_LEN   64u   /* caller buffer must be at least this   */

#define UDS_VIN_LEN            17u
#define UDS_PART_NUMBER_LEN    10u
#define UDS_MAX_DTC             8u

/* ------------------------------------------------------------- constants -- */

/* Service identifiers */
#define UDS_SID_SESSION_CONTROL     0x10u
#define UDS_SID_CLEAR_DTC           0x14u
#define UDS_SID_READ_DTC_INFO       0x19u
#define UDS_SID_READ_DATA_BY_ID     0x22u
#define UDS_SID_SECURITY_ACCESS     0x27u
#define UDS_SID_WRITE_DATA_BY_ID    0x2Eu
#define UDS_SID_TESTER_PRESENT      0x3Eu

#define UDS_POSITIVE_RESPONSE_OFFSET 0x40u
#define UDS_NEGATIVE_RESPONSE_SID    0x7Fu

/* Negative response codes */
#define UDS_NRC_SERVICE_NOT_SUPPORTED             0x11u
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED         0x12u
#define UDS_NRC_INVALID_LENGTH                    0x13u
#define UDS_NRC_CONDITIONS_NOT_CORRECT            0x22u
#define UDS_NRC_REQUEST_OUT_OF_RANGE              0x31u
#define UDS_NRC_SECURITY_ACCESS_DENIED            0x33u
#define UDS_NRC_INVALID_KEY                       0x35u
#define UDS_NRC_EXCEED_NUMBER_OF_ATTEMPTS         0x36u
#define UDS_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION  0x7Fu

/* Data identifiers */
#define UDS_DID_VIN                 0xF190u
#define UDS_DID_PART_NUMBER         0xF187u
#define UDS_DID_PAD_WEAR            0x2A01u
#define UDS_DID_RESERVOIR_PRESSURE  0x2A02u

/* Sub-function bit 7 = suppressPosRspMsgIndicationBit */
#define UDS_SUPPRESS_POS_RSP_BIT    0x80u
#define UDS_SUBFUNC_MASK            0x7Fu

/* Timing */
#define UDS_S3_TIMEOUT_MS          5000u   /* extended session -> default     */
#define UDS_SECURITY_LOCKOUT_MS   10000u   /* after 3 failed key attempts     */
#define UDS_SECURITY_MAX_ATTEMPTS     3u

/* Security */
#define UDS_SECURITY_KEY_XOR   0x5A5A5A5Au
#define UDS_SECURITY_KEY_ROTL           3u

/* ----------------------------------------------------------------- types -- */

typedef enum {
    UDS_SESSION_DEFAULT  = 0x01,
    UDS_SESSION_EXTENDED = 0x03
} uds_session_t;

typedef enum {
    UDS_SEC_LOCKED    = 0,  /* no seed handed out yet                         */
    UDS_SEC_SEED_SENT = 1,  /* seed delivered, waiting for the key            */
    UDS_SEC_UNLOCKED  = 2   /* level 0x01 unlocked                            */
} uds_sec_state_t;

/* Result of uds_handle_request(). */
typedef enum {
    UDS_RESULT_RESPONSE      = 0,  /* *resp_len bytes written                 */
    UDS_RESULT_NO_RESPONSE   = 1,  /* suppressed positive response            */
    UDS_RESULT_BUFFER_TOO_SMALL = 2 /* caller buffer too small; nothing written */
} uds_result_t;

typedef struct {
    uint32_t code;      /* 3-byte DTC, stored right aligned                   */
    uint8_t  status;    /* ISO 14229 status-of-DTC byte                       */
    uint8_t  valid;     /* 0 = empty slot                                     */
} uds_dtc_t;

typedef struct {
    /* --- session ------------------------------------------------------- */
    uds_session_t session;
    uint32_t      last_activity_ms;   /* refreshed by every handled request  */

    /* --- security ------------------------------------------------------ */
    uds_sec_state_t sec_state;
    uint32_t        seed;             /* seed currently outstanding          */
    uint32_t        seed_rng;         /* deterministic seed generator state  */
    uint8_t         failed_attempts;
    uint8_t         locked_out;
    uint32_t        lockout_start_ms;

    /* --- data ---------------------------------------------------------- */
    uint8_t  vin[UDS_VIN_LEN];
    uint8_t  part_number[UDS_PART_NUMBER_LEN];
    uint8_t  pad_wear[4];             /* one per wheel, percent remaining    */
    uint16_t reservoir_pressure;      /* 0.01 bar / bit                      */

    uds_dtc_t dtc[UDS_MAX_DTC];
} uds_ctx_t;

/* ------------------------------------------------------------------- API -- */

/*
 * Reset the context to the power-on state: default session, security locked,
 * factory data content and the default DTC set. `rng_seed` makes the seed
 * generator deterministic; use the same value on both implementations when
 * running a differential test.
 */
void uds_init(uds_ctx_t *ctx, uint32_t rng_seed);

/*
 * Handle one request.
 *
 * ctx       - session context, must have been passed to uds_init()
 * now_ms    - monotonically non-decreasing millisecond timestamp
 * req       - request bytes (may be NULL only if req_len == 0)
 * req_len   - number of request bytes
 * resp      - caller-owned response buffer
 * resp_cap  - capacity of `resp`, must be >= UDS_MAX_RESPONSE_LEN
 * resp_len  - out: number of bytes written to `resp`
 *
 * Returns UDS_RESULT_RESPONSE, UDS_RESULT_NO_RESPONSE (suppressed positive
 * response) or UDS_RESULT_BUFFER_TOO_SMALL.
 *
 * A request of length 0, a request longer than UDS_MAX_REQUEST_LEN and every
 * malformed request are answered with a negative response; the handler never
 * reads outside `req[0 .. req_len-1]`.
 */
uds_result_t uds_handle_request(uds_ctx_t *ctx,
                                uint32_t   now_ms,
                                const uint8_t *req,
                                size_t     req_len,
                                uint8_t   *resp,
                                size_t     resp_cap,
                                size_t    *resp_len);

/* Key derivation, exposed so a tester can compute the expected key:
 *   key = rotate_left_32(seed XOR 0x5A5A5A5A, 3)
 * The XOR is applied first, the rotation second. */
uint32_t uds_compute_key(uint32_t seed);

#endif /* UDS_H */
