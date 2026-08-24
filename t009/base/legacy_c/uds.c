/*
 * uds.c -- UDS (ISO 14229) diagnostic session handler, trailer brake ECU.
 *
 * See uds.h for the API and the time-injection contract, and BEHAVIOUR.md for
 * the decisions the ISO standard leaves open (check order, suppress-bit scope,
 * seed generation, ...). This module is the *reference behaviour* for T009:
 * where the standard and this file disagree, this file wins.
 */

#include "uds.h"

/* ------------------------------------------------------------------------ */
/* helpers                                                                   */
/* ------------------------------------------------------------------------ */

static uint32_t elapsed_since(uint32_t now_ms, uint32_t start_ms)
{
    return (uint32_t)(now_ms - start_ms);   /* wrap-around safe */
}

static uint32_t rotl32(uint32_t v, unsigned n)
{
    n &= 31u;
    if (n == 0u) {
        return v;
    }
    return (uint32_t)((v << n) | (v >> (32u - n)));
}

uint32_t uds_compute_key(uint32_t seed)
{
    return rotl32(seed ^ UDS_SECURITY_KEY_XOR, UDS_SECURITY_KEY_ROTL);
}

/* Deterministic seed generator (Numerical Recipes LCG). A seed of 0 is never
 * handed out, so that "seed == 0" can keep its ISO meaning of "already
 * unlocked". */
static uint32_t next_seed(uds_ctx_t *ctx)
{
    uint32_t s;
    ctx->seed_rng = (uint32_t)(ctx->seed_rng * 1664525u + 1013904223u);
    s = ctx->seed_rng;
    if (s == 0u) {
        s = 1u;
    }
    return s;
}

/* Response builders. All of them write into the caller buffer, which has
 * already been checked to be at least UDS_MAX_RESPONSE_LEN bytes. */

static uds_result_t negative_response(uint8_t sid, uint8_t nrc,
                                      uint8_t *resp, size_t *resp_len)
{
    resp[0] = UDS_NEGATIVE_RESPONSE_SID;
    resp[1] = sid;
    resp[2] = nrc;
    *resp_len = 3u;
    return UDS_RESULT_RESPONSE;
}

static uds_result_t positive_response(uint8_t suppress, size_t len,
                                      size_t *resp_len)
{
    if (suppress) {
        *resp_len = 0u;
        return UDS_RESULT_NO_RESPONSE;
    }
    *resp_len = len;
    return UDS_RESULT_RESPONSE;
}

/* Drop the unlock, keep the lockout: switching sessions must not be a way out
 * of the security lockout. */
static void security_relock(uds_ctx_t *ctx)
{
    ctx->sec_state = UDS_SEC_LOCKED;
    ctx->seed      = 0u;
}

/* ------------------------------------------------------------------------ */
/* init                                                                      */
/* ------------------------------------------------------------------------ */

static const uint8_t FACTORY_VIN[UDS_VIN_LEN] = {
    'W','D','B','9','6','3','4','0','3','1','L','1','2','3','4','5','6'
};

static const uint8_t FACTORY_PART_NUMBER[UDS_PART_NUMBER_LEN] = {
    'A','0','0','0','4','2','9','7','8','3'
};

void uds_init(uds_ctx_t *ctx, uint32_t rng_seed)
{
    size_t i;

    if (ctx == NULL) {
        return;
    }

    ctx->session          = UDS_SESSION_DEFAULT;
    ctx->last_activity_ms = 0u;

    ctx->sec_state        = UDS_SEC_LOCKED;
    ctx->seed             = 0u;
    ctx->seed_rng         = rng_seed;
    ctx->failed_attempts  = 0u;
    ctx->locked_out       = 0u;
    ctx->lockout_start_ms = 0u;

    for (i = 0u; i < UDS_VIN_LEN; i++) {
        ctx->vin[i] = FACTORY_VIN[i];
    }
    for (i = 0u; i < UDS_PART_NUMBER_LEN; i++) {
        ctx->part_number[i] = FACTORY_PART_NUMBER[i];
    }

    ctx->pad_wear[0] = 88u;   /* front left,  % remaining */
    ctx->pad_wear[1] = 85u;   /* front right              */
    ctx->pad_wear[2] = 62u;   /* rear left                */
    ctx->pad_wear[3] = 60u;   /* rear right               */

    ctx->reservoir_pressure = 820u;   /* 8.20 bar */

    for (i = 0u; i < UDS_MAX_DTC; i++) {
        ctx->dtc[i].code   = 0u;
        ctx->dtc[i].status = 0u;
        ctx->dtc[i].valid  = 0u;
    }
    /* Status byte bit 0 = testFailed -> "currently active".
     * Bit 3 = confirmedDTC -> "stored". */
    ctx->dtc[0].code = 0xB12004u; ctx->dtc[0].status = 0x2Fu; ctx->dtc[0].valid = 1u; /* active */
    ctx->dtc[1].code = 0xC00512u; ctx->dtc[1].status = 0x08u; ctx->dtc[1].valid = 1u; /* stored */
    ctx->dtc[2].code = 0xD1010Au; ctx->dtc[2].status = 0x28u; ctx->dtc[2].valid = 1u; /* stored */
    ctx->dtc[3].code = 0x912345u; ctx->dtc[3].status = 0x09u; ctx->dtc[3].valid = 1u; /* active */
}

/* ------------------------------------------------------------------------ */
/* services                                                                  */
/* ------------------------------------------------------------------------ */

/* 0x10 DiagnosticSessionControl */
static uds_result_t svc_session_control(uds_ctx_t *ctx, const uint8_t *req,
                                        size_t req_len, uint8_t *resp,
                                        size_t *resp_len)
{
    uint8_t suppress;
    uint8_t sub;

    if (req_len != 2u) {
        return negative_response(UDS_SID_SESSION_CONTROL,
                                 UDS_NRC_INVALID_LENGTH, resp, resp_len);
    }

    suppress = (uint8_t)(req[1] & UDS_SUPPRESS_POS_RSP_BIT);
    sub      = (uint8_t)(req[1] & UDS_SUBFUNC_MASK);

    if (sub != (uint8_t)UDS_SESSION_DEFAULT && sub != (uint8_t)UDS_SESSION_EXTENDED) {
        return negative_response(UDS_SID_SESSION_CONTROL,
                                 UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, resp, resp_len);
    }

    /* Every accepted session control drops the unlock, even when it selects
     * the session that is already active. */
    ctx->session = (uds_session_t)sub;
    security_relock(ctx);

    resp[0] = (uint8_t)(UDS_SID_SESSION_CONTROL + UDS_POSITIVE_RESPONSE_OFFSET);
    resp[1] = sub;
    resp[2] = 0x00u;  /* P2_server_max   = 50 ms      */
    resp[3] = 0x32u;
    resp[4] = 0x01u;  /* P2*_server_max  = 5000 ms    */
    resp[5] = 0xF4u;
    return positive_response(suppress, 6u, resp_len);
}

/* 0x3E TesterPresent */
static uds_result_t svc_tester_present(uds_ctx_t *ctx, const uint8_t *req,
                                       size_t req_len, uint8_t *resp,
                                       size_t *resp_len)
{
    uint8_t suppress;
    uint8_t sub;

    (void)ctx;

    if (req_len != 2u) {
        return negative_response(UDS_SID_TESTER_PRESENT,
                                 UDS_NRC_INVALID_LENGTH, resp, resp_len);
    }

    suppress = (uint8_t)(req[1] & UDS_SUPPRESS_POS_RSP_BIT);
    sub      = (uint8_t)(req[1] & UDS_SUBFUNC_MASK);

    if (sub != 0x00u) {
        return negative_response(UDS_SID_TESTER_PRESENT,
                                 UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, resp, resp_len);
    }

    resp[0] = (uint8_t)(UDS_SID_TESTER_PRESENT + UDS_POSITIVE_RESPONSE_OFFSET);
    resp[1] = 0x00u;
    return positive_response(suppress, 2u, resp_len);
}

/* 0x27 SecurityAccess */
static uds_result_t svc_security_access(uds_ctx_t *ctx, uint32_t now_ms,
                                        const uint8_t *req, size_t req_len,
                                        uint8_t *resp, size_t *resp_len)
{
    uint8_t  suppress;
    uint8_t  sub;
    uint32_t key;
    uint32_t expected;

    if (req_len < 2u) {
        return negative_response(UDS_SID_SECURITY_ACCESS,
                                 UDS_NRC_INVALID_LENGTH, resp, resp_len);
    }

    suppress = (uint8_t)(req[1] & UDS_SUPPRESS_POS_RSP_BIT);
    sub      = (uint8_t)(req[1] & UDS_SUBFUNC_MASK);

    if (ctx->session != UDS_SESSION_EXTENDED) {
        return negative_response(UDS_SID_SECURITY_ACCESS,
                                 UDS_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION,
                                 resp, resp_len);
    }

    if (ctx->locked_out) {
        return negative_response(UDS_SID_SECURITY_ACCESS,
                                 UDS_NRC_EXCEED_NUMBER_OF_ATTEMPTS, resp, resp_len);
    }

    if (sub == 0x01u) {                     /* requestSeed */
        uint32_t seed;

        if (req_len != 2u) {
            return negative_response(UDS_SID_SECURITY_ACCESS,
                                     UDS_NRC_INVALID_LENGTH, resp, resp_len);
        }

        if (ctx->sec_state == UDS_SEC_UNLOCKED) {
            seed = 0u;                      /* ISO: already unlocked */
        } else {
            seed = next_seed(ctx);
            ctx->seed      = seed;
            ctx->sec_state = UDS_SEC_SEED_SENT;
        }

        resp[0] = (uint8_t)(UDS_SID_SECURITY_ACCESS + UDS_POSITIVE_RESPONSE_OFFSET);
        resp[1] = 0x01u;
        resp[2] = (uint8_t)(seed >> 24);
        resp[3] = (uint8_t)(seed >> 16);
        resp[4] = (uint8_t)(seed >> 8);
        resp[5] = (uint8_t)(seed);
        return positive_response(suppress, 6u, resp_len);
    }

    if (sub == 0x02u) {                     /* sendKey */
        if (req_len != 6u) {
            return negative_response(UDS_SID_SECURITY_ACCESS,
                                     UDS_NRC_INVALID_LENGTH, resp, resp_len);
        }

        if (ctx->sec_state != UDS_SEC_SEED_SENT) {
            return negative_response(UDS_SID_SECURITY_ACCESS,
                                     UDS_NRC_CONDITIONS_NOT_CORRECT, resp, resp_len);
        }

        key = ((uint32_t)req[2] << 24) | ((uint32_t)req[3] << 16)
            | ((uint32_t)req[4] << 8)  | (uint32_t)req[5];
        expected = uds_compute_key(ctx->seed);

        if (key == expected) {
            ctx->sec_state       = UDS_SEC_UNLOCKED;
            ctx->failed_attempts = 0u;
            ctx->seed            = 0u;
            resp[0] = (uint8_t)(UDS_SID_SECURITY_ACCESS + UDS_POSITIVE_RESPONSE_OFFSET);
            resp[1] = 0x02u;
            return positive_response(suppress, 2u, resp_len);
        }

        /* Wrong key: the outstanding seed is burned, a new one must be
         * requested before the next attempt. */
        ctx->sec_state = UDS_SEC_LOCKED;
        ctx->seed      = 0u;
        ctx->failed_attempts++;

        if (ctx->failed_attempts >= UDS_SECURITY_MAX_ATTEMPTS) {
            ctx->locked_out       = 1u;
            ctx->lockout_start_ms = now_ms;
            return negative_response(UDS_SID_SECURITY_ACCESS,
                                     UDS_NRC_EXCEED_NUMBER_OF_ATTEMPTS,
                                     resp, resp_len);
        }
        return negative_response(UDS_SID_SECURITY_ACCESS,
                                 UDS_NRC_INVALID_KEY, resp, resp_len);
    }

    return negative_response(UDS_SID_SECURITY_ACCESS,
                             UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, resp, resp_len);
}

/* 0x22 ReadDataByIdentifier */
static uds_result_t svc_read_data_by_id(uds_ctx_t *ctx, const uint8_t *req,
                                        size_t req_len, uint8_t *resp,
                                        size_t *resp_len)
{
    uint16_t did;
    size_t   n = 0u;
    size_t   i;

    if (req_len != 3u) {
        return negative_response(UDS_SID_READ_DATA_BY_ID,
                                 UDS_NRC_INVALID_LENGTH, resp, resp_len);
    }

    did = (uint16_t)(((uint16_t)req[1] << 8) | (uint16_t)req[2]);

    resp[0] = (uint8_t)(UDS_SID_READ_DATA_BY_ID + UDS_POSITIVE_RESPONSE_OFFSET);
    resp[1] = req[1];
    resp[2] = req[2];
    n = 3u;

    switch (did) {
    case UDS_DID_VIN:
        for (i = 0u; i < UDS_VIN_LEN; i++) {
            resp[n++] = ctx->vin[i];
        }
        break;
    case UDS_DID_PART_NUMBER:
        for (i = 0u; i < UDS_PART_NUMBER_LEN; i++) {
            resp[n++] = ctx->part_number[i];
        }
        break;
    case UDS_DID_PAD_WEAR:
        for (i = 0u; i < 4u; i++) {
            resp[n++] = ctx->pad_wear[i];
        }
        break;
    case UDS_DID_RESERVOIR_PRESSURE:
        resp[n++] = (uint8_t)(ctx->reservoir_pressure >> 8);
        resp[n++] = (uint8_t)(ctx->reservoir_pressure);
        break;
    default:
        return negative_response(UDS_SID_READ_DATA_BY_ID,
                                 UDS_NRC_REQUEST_OUT_OF_RANGE, resp, resp_len);
    }

    *resp_len = n;
    return UDS_RESULT_RESPONSE;
}

/* 0x2E WriteDataByIdentifier */
static uds_result_t svc_write_data_by_id(uds_ctx_t *ctx, const uint8_t *req,
                                         size_t req_len, uint8_t *resp,
                                         size_t *resp_len)
{
    uint16_t did;
    size_t   i;

    if (req_len < 3u) {
        return negative_response(UDS_SID_WRITE_DATA_BY_ID,
                                 UDS_NRC_INVALID_LENGTH, resp, resp_len);
    }

    if (ctx->session != UDS_SESSION_EXTENDED) {
        return negative_response(UDS_SID_WRITE_DATA_BY_ID,
                                 UDS_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION,
                                 resp, resp_len);
    }

    if (ctx->sec_state != UDS_SEC_UNLOCKED) {
        return negative_response(UDS_SID_WRITE_DATA_BY_ID,
                                 UDS_NRC_SECURITY_ACCESS_DENIED, resp, resp_len);
    }

    did = (uint16_t)(((uint16_t)req[1] << 8) | (uint16_t)req[2]);
    if (did != UDS_DID_PART_NUMBER) {
        return negative_response(UDS_SID_WRITE_DATA_BY_ID,
                                 UDS_NRC_REQUEST_OUT_OF_RANGE, resp, resp_len);
    }

    if (req_len != (3u + UDS_PART_NUMBER_LEN)) {
        return negative_response(UDS_SID_WRITE_DATA_BY_ID,
                                 UDS_NRC_INVALID_LENGTH, resp, resp_len);
    }

    for (i = 0u; i < UDS_PART_NUMBER_LEN; i++) {
        ctx->part_number[i] = req[3u + i];
    }

    resp[0] = (uint8_t)(UDS_SID_WRITE_DATA_BY_ID + UDS_POSITIVE_RESPONSE_OFFSET);
    resp[1] = req[1];
    resp[2] = req[2];
    *resp_len = 3u;
    return UDS_RESULT_RESPONSE;
}

/* 0x14 ClearDiagnosticInformation */
static uds_result_t svc_clear_dtc(uds_ctx_t *ctx, const uint8_t *req,
                                  size_t req_len, uint8_t *resp,
                                  size_t *resp_len)
{
    uint32_t group;
    size_t   i;

    if (req_len != 4u) {
        return negative_response(UDS_SID_CLEAR_DTC,
                                 UDS_NRC_INVALID_LENGTH, resp, resp_len);
    }

    group = ((uint32_t)req[1] << 16) | ((uint32_t)req[2] << 8) | (uint32_t)req[3];
    if (group != 0xFFFFFFu) {          /* only "all groups" is supported */
        return negative_response(UDS_SID_CLEAR_DTC,
                                 UDS_NRC_REQUEST_OUT_OF_RANGE, resp, resp_len);
    }

    /* Stored DTCs are dropped; a DTC whose testFailed bit is set is currently
     * present and survives the clear. */
    for (i = 0u; i < UDS_MAX_DTC; i++) {
        if (ctx->dtc[i].valid && (ctx->dtc[i].status & 0x01u) == 0u) {
            ctx->dtc[i].valid  = 0u;
            ctx->dtc[i].code   = 0u;
            ctx->dtc[i].status = 0u;
        }
    }

    resp[0] = (uint8_t)(UDS_SID_CLEAR_DTC + UDS_POSITIVE_RESPONSE_OFFSET);
    *resp_len = 1u;
    return UDS_RESULT_RESPONSE;
}

/* 0x19 ReadDTCInformation, sub-function 0x02 reportDTCByStatusMask */
static uds_result_t svc_read_dtc_info(uds_ctx_t *ctx, const uint8_t *req,
                                      size_t req_len, uint8_t *resp,
                                      size_t *resp_len)
{
    uint8_t suppress;
    uint8_t sub;
    uint8_t mask;
    size_t  n;
    size_t  i;

    if (req_len < 2u) {
        return negative_response(UDS_SID_READ_DTC_INFO,
                                 UDS_NRC_INVALID_LENGTH, resp, resp_len);
    }

    suppress = (uint8_t)(req[1] & UDS_SUPPRESS_POS_RSP_BIT);
    sub      = (uint8_t)(req[1] & UDS_SUBFUNC_MASK);

    if (sub != 0x02u) {
        return negative_response(UDS_SID_READ_DTC_INFO,
                                 UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, resp, resp_len);
    }

    if (req_len != 3u) {
        return negative_response(UDS_SID_READ_DTC_INFO,
                                 UDS_NRC_INVALID_LENGTH, resp, resp_len);
    }

    mask = req[2];

    resp[0] = (uint8_t)(UDS_SID_READ_DTC_INFO + UDS_POSITIVE_RESPONSE_OFFSET);
    resp[1] = 0x02u;
    resp[2] = 0xFFu;                   /* DTCStatusAvailabilityMask */
    n = 3u;

    for (i = 0u; i < UDS_MAX_DTC; i++) {
        if (!ctx->dtc[i].valid) {
            continue;
        }
        if ((ctx->dtc[i].status & mask) == 0u) {
            continue;
        }
        resp[n++] = (uint8_t)(ctx->dtc[i].code >> 16);
        resp[n++] = (uint8_t)(ctx->dtc[i].code >> 8);
        resp[n++] = (uint8_t)(ctx->dtc[i].code);
        resp[n++] = ctx->dtc[i].status;
    }

    return positive_response(suppress, n, resp_len);
}

/* ------------------------------------------------------------------------ */
/* dispatch                                                                  */
/* ------------------------------------------------------------------------ */

uds_result_t uds_handle_request(uds_ctx_t *ctx,
                                uint32_t   now_ms,
                                const uint8_t *req,
                                size_t     req_len,
                                uint8_t   *resp,
                                size_t     resp_cap,
                                size_t    *resp_len)
{
    if (ctx == NULL || resp == NULL || resp_len == NULL) {
        return UDS_RESULT_BUFFER_TOO_SMALL;
    }
    if (resp_cap < UDS_MAX_RESPONSE_LEN) {
        return UDS_RESULT_BUFFER_TOO_SMALL;
    }

    *resp_len = 0u;

    /* --- timers, evaluated before dispatch ----------------------------- */

    if (ctx->locked_out &&
        elapsed_since(now_ms, ctx->lockout_start_ms) >= UDS_SECURITY_LOCKOUT_MS) {
        ctx->locked_out      = 0u;
        ctx->failed_attempts = 0u;
    }

    if (ctx->session == UDS_SESSION_EXTENDED &&
        elapsed_since(now_ms, ctx->last_activity_ms) >= UDS_S3_TIMEOUT_MS) {
        ctx->session = UDS_SESSION_DEFAULT;
        security_relock(ctx);
    }

    /* --- request sanity ------------------------------------------------- */

    if (req == NULL || req_len == 0u) {
        /* No SID to echo; 0x00 is used as the placeholder. */
        return negative_response(0x00u, UDS_NRC_INVALID_LENGTH, resp, resp_len);
    }
    if (req_len > UDS_MAX_REQUEST_LEN) {
        return negative_response(req[0], UDS_NRC_INVALID_LENGTH, resp, resp_len);
    }

    /* Any request that reaches dispatch refreshes the S3 timer. */
    ctx->last_activity_ms = now_ms;

    /* --- dispatch -------------------------------------------------------- */

    switch (req[0]) {
    case UDS_SID_SESSION_CONTROL:
        return svc_session_control(ctx, req, req_len, resp, resp_len);
    case UDS_SID_CLEAR_DTC:
        return svc_clear_dtc(ctx, req, req_len, resp, resp_len);
    case UDS_SID_READ_DTC_INFO:
        return svc_read_dtc_info(ctx, req, req_len, resp, resp_len);
    case UDS_SID_READ_DATA_BY_ID:
        return svc_read_data_by_id(ctx, req, req_len, resp, resp_len);
    case UDS_SID_SECURITY_ACCESS:
        return svc_security_access(ctx, now_ms, req, req_len, resp, resp_len);
    case UDS_SID_WRITE_DATA_BY_ID:
        return svc_write_data_by_id(ctx, req, req_len, resp, resp_len);
    case UDS_SID_TESTER_PRESENT:
        return svc_tester_present(ctx, req, req_len, resp, resp_len);
    default:
        return negative_response(req[0], UDS_NRC_SERVICE_NOT_SUPPORTED,
                                 resp, resp_len);
    }
}
