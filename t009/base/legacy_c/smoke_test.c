/*
 * smoke_test.c -- build sanity check for the reference implementation.
 *
 * This is NOT the deliverable test suite; it only proves the module compiles
 * and behaves as BEHAVIOUR.md describes on a handful of sequences. Writing the
 * real unit tests, the differential corpus and the fuzz target is the task.
 *
 *   make smoke && ./smoke
 */
#include "uds.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const char *what, const uint8_t *got, size_t got_len,
                   const uint8_t *want, size_t want_len)
{
    size_t i;
    if (got_len == want_len && (want_len == 0u || memcmp(got, want, want_len) == 0)) {
        printf("  ok   %s\n", what);
        return;
    }
    failures++;
    printf("  FAIL %s\n       got :", what);
    for (i = 0u; i < got_len; i++) printf(" %02X", got[i]);
    printf("\n       want:");
    for (i = 0u; i < want_len; i++) printf(" %02X", want[i]);
    printf("\n");
}

int main(void)
{
    uds_ctx_t ctx;
    uint8_t   resp[UDS_MAX_RESPONSE_LEN];
    size_t    n;
    uint32_t  seed, key;

    uds_init(&ctx, 0x12345678u);

    /* unknown service */
    {
        const uint8_t req[] = {0x11};
        const uint8_t want[] = {0x7F, 0x11, 0x11};
        uds_handle_request(&ctx, 0, req, sizeof req, resp, sizeof resp, &n);
        expect("unknown SID -> 0x11", resp, n, want, sizeof want);
    }

    /* empty request */
    {
        const uint8_t want[] = {0x7F, 0x00, 0x13};
        uds_handle_request(&ctx, 0, NULL, 0, resp, sizeof resp, &n);
        expect("empty request -> 0x13", resp, n, want, sizeof want);
    }

    /* read VIN in default session */
    {
        const uint8_t req[] = {0x22, 0xF1, 0x90};
        const uint8_t want[] = {0x62, 0xF1, 0x90,
                                'W','D','B','9','6','3','4','0','3','1',
                                'L','1','2','3','4','5','6'};
        uds_handle_request(&ctx, 0, req, sizeof req, resp, sizeof resp, &n);
        expect("read VIN", resp, n, want, sizeof want);
    }

    /* unknown DID */
    {
        const uint8_t req[] = {0x22, 0x12, 0x34};
        const uint8_t want[] = {0x7F, 0x22, 0x31};
        uds_handle_request(&ctx, 0, req, sizeof req, resp, sizeof resp, &n);
        expect("unknown DID -> 0x31", resp, n, want, sizeof want);
    }

    /* write in default session -> 0x7F */
    {
        const uint8_t req[] = {0x2E, 0xF1, 0x87, '1','2','3','4','5','6','7','8','9','0'};
        const uint8_t want[] = {0x7F, 0x2E, 0x7F};
        uds_handle_request(&ctx, 0, req, sizeof req, resp, sizeof resp, &n);
        expect("write in default session -> 0x7F", resp, n, want, sizeof want);
    }

    /* enter extended session */
    {
        const uint8_t req[] = {0x10, 0x03};
        const uint8_t want[] = {0x50, 0x03, 0x00, 0x32, 0x01, 0xF4};
        uds_handle_request(&ctx, 1000, req, sizeof req, resp, sizeof resp, &n);
        expect("enter extended session", resp, n, want, sizeof want);
    }

    /* write without security -> 0x33 */
    {
        const uint8_t req[] = {0x2E, 0xF1, 0x87, '1','2','3','4','5','6','7','8','9','0'};
        const uint8_t want[] = {0x7F, 0x2E, 0x33};
        uds_handle_request(&ctx, 1100, req, sizeof req, resp, sizeof resp, &n);
        expect("write without security -> 0x33", resp, n, want, sizeof want);
    }

    /* seed / key round trip */
    {
        const uint8_t req_seed[] = {0x27, 0x01};
        uint8_t req_key[6] = {0x27, 0x02, 0, 0, 0, 0};
        const uint8_t want_key[] = {0x67, 0x02};

        uds_handle_request(&ctx, 1200, req_seed, sizeof req_seed, resp, sizeof resp, &n);
        if (n != 6u || resp[0] != 0x67 || resp[1] != 0x01) {
            failures++;
            printf("  FAIL requestSeed shape\n");
            seed = 0u;
        } else {
            printf("  ok   requestSeed\n");
            seed = ((uint32_t)resp[2] << 24) | ((uint32_t)resp[3] << 16)
                 | ((uint32_t)resp[4] << 8)  | (uint32_t)resp[5];
        }
        key = uds_compute_key(seed);
        req_key[2] = (uint8_t)(key >> 24);
        req_key[3] = (uint8_t)(key >> 16);
        req_key[4] = (uint8_t)(key >> 8);
        req_key[5] = (uint8_t)(key);
        uds_handle_request(&ctx, 1300, req_key, sizeof req_key, resp, sizeof resp, &n);
        expect("sendKey correct -> unlocked", resp, n, want_key, sizeof want_key);
    }

    /* now the write succeeds */
    {
        const uint8_t req[] = {0x2E, 0xF1, 0x87, 'B','9','9','9','1','2','3','4','5','6'};
        const uint8_t want[] = {0x6E, 0xF1, 0x87};
        const uint8_t rd[] = {0x22, 0xF1, 0x87};
        const uint8_t want_rd[] = {0x62, 0xF1, 0x87, 'B','9','9','9','1','2','3','4','5','6'};
        uds_handle_request(&ctx, 1400, req, sizeof req, resp, sizeof resp, &n);
        expect("write part number", resp, n, want, sizeof want);
        uds_handle_request(&ctx, 1500, rd, sizeof rd, resp, sizeof resp, &n);
        expect("read back part number", resp, n, want_rd, sizeof want_rd);
    }

    /* suppressPosRsp on TesterPresent */
    {
        const uint8_t req[] = {0x3E, 0x80};
        uds_result_t r = uds_handle_request(&ctx, 1600, req, sizeof req,
                                            resp, sizeof resp, &n);
        if (r == UDS_RESULT_NO_RESPONSE && n == 0u) {
            printf("  ok   TesterPresent suppress bit\n");
        } else {
            failures++;
            printf("  FAIL TesterPresent suppress bit (r=%d n=%zu)\n", (int)r, n);
        }
    }

    /* S3 timeout drops back to the default session */
    {
        const uint8_t req[] = {0x2E, 0xF1, 0x87, '1','2','3','4','5','6','7','8','9','0'};
        const uint8_t want[] = {0x7F, 0x2E, 0x7F};
        uds_handle_request(&ctx, 1600 + UDS_S3_TIMEOUT_MS, req, sizeof req,
                           resp, sizeof resp, &n);
        expect("S3 timeout -> default session", resp, n, want, sizeof want);
    }

    /* DTC read + clear */
    {
        const uint8_t rd[] = {0x19, 0x02, 0xFF};
        const uint8_t want_rd[] = {0x59, 0x02, 0xFF,
                                   0xB1, 0x20, 0x04, 0x2F,
                                   0xC0, 0x05, 0x12, 0x08,
                                   0xD1, 0x01, 0x0A, 0x28,
                                   0x91, 0x23, 0x45, 0x09};
        const uint8_t clr[] = {0x14, 0xFF, 0xFF, 0xFF};
        const uint8_t want_clr[] = {0x54};
        const uint8_t want_after[] = {0x59, 0x02, 0xFF,
                                      0xB1, 0x20, 0x04, 0x2F,
                                      0x91, 0x23, 0x45, 0x09};
        uds_handle_request(&ctx, 20000, rd, sizeof rd, resp, sizeof resp, &n);
        expect("reportDTCByStatusMask", resp, n, want_rd, sizeof want_rd);
        uds_handle_request(&ctx, 20001, clr, sizeof clr, resp, sizeof resp, &n);
        expect("clearDiagnosticInformation", resp, n, want_clr, sizeof want_clr);
        uds_handle_request(&ctx, 20002, rd, sizeof rd, resp, sizeof resp, &n);
        expect("stored DTCs gone, active kept", resp, n, want_after, sizeof want_after);
    }

    /* three bad keys -> lockout */
    {
        const uint8_t ext[] = {0x10, 0x03};
        const uint8_t rs[]  = {0x27, 0x01};
        const uint8_t bad[] = {0x27, 0x02, 0xDE, 0xAD, 0xBE, 0xEF};
        const uint8_t want35[] = {0x7F, 0x27, 0x35};
        const uint8_t want36[] = {0x7F, 0x27, 0x36};
        uint32_t t = 30000;
        int i;

        uds_handle_request(&ctx, t, ext, sizeof ext, resp, sizeof resp, &n);
        for (i = 0; i < 3; i++) {
            t += 10;
            uds_handle_request(&ctx, t, rs, sizeof rs, resp, sizeof resp, &n);
            t += 10;
            uds_handle_request(&ctx, t, bad, sizeof bad, resp, sizeof resp, &n);
            if (i < 2) {
                expect("bad key -> 0x35", resp, n, want35, sizeof want35);
            } else {
                expect("third bad key -> 0x36", resp, n, want36, sizeof want36);
            }
        }
        t += 10;
        uds_handle_request(&ctx, t, rs, sizeof rs, resp, sizeof resp, &n);
        expect("locked out -> 0x36", resp, n, want36, sizeof want36);

        /* after the lockout expires a seed can be requested again */
        t += UDS_SECURITY_LOCKOUT_MS;
        uds_handle_request(&ctx, t, ext, sizeof ext, resp, sizeof resp, &n);
        uds_handle_request(&ctx, t, rs, sizeof rs, resp, sizeof resp, &n);
        if (n == 6u && resp[0] == 0x67) {
            printf("  ok   lockout expires\n");
        } else {
            failures++;
            printf("  FAIL lockout expires\n");
        }
    }

    printf(failures ? "\nSMOKE TEST FAILED (%d)\n" : "\nsmoke test passed (%d failures)\n",
           failures);
    return failures ? 1 : 0;
}
