# Reference behaviour notes (`legacy_c`)

ISO 14229 leaves a number of things open, and a differential test is only
meaningful if both implementations resolve them the same way. This file records
every decision this reference makes. **The Rust port must match this file, not
your reading of the standard.** If you believe a decision here is wrong, that is
a legitimate finding — record it as a documented, justified deviation rather
than silently changing one side.

## General

- **Single request in, single response out.** No multi-frame transport, no
  response-pending (`0x78`) handling.
- `now_ms` is injected and must be monotonically non-decreasing. All timer
  comparisons use unsigned difference arithmetic, so a 32-bit wrap is handled.
- Timers (security lockout, S3) are evaluated **before** the request is
  dispatched.
- **Every** request that reaches dispatch refreshes the S3 timer, not only
  TesterPresent. A request rejected for being empty or oversized does *not*
  refresh it.
- An **empty** request (`req_len == 0`) is answered `7F 00 13` — there is no SID
  to echo, so `0x00` is used as the placeholder.
- A request longer than `UDS_MAX_REQUEST_LEN` (64) is answered `7F <sid> 13`.
- An unknown SID yields NRC `0x11`.
- `resp_cap` smaller than `UDS_MAX_RESPONSE_LEN` (64) returns
  `UDS_RESULT_BUFFER_TOO_SMALL` and writes nothing. This is an API misuse, not a
  protocol event.

## suppressPosRspMsgIndicationBit

Applied **uniformly** to every sub-function service — `0x10`, `0x19`, `0x27`,
`0x3E`. (ISO restricts it more narrowly; uniform handling was chosen because it
is easier to specify and to test.) It suppresses **positive** responses only;
negative responses are always sent.

## 0x10 DiagnosticSessionControl

- Length must be exactly 2, else `0x13`.
- Sub-function `0x01` / `0x03` only, else `0x12`.
- The positive response is `50 <sub> 00 32 01 F4`
  (P2_server_max = 50 ms, P2*_server_max = 5000 ms).
- **Any** accepted session control drops the security unlock, including a
  request that selects the session that is already active.
- The security **lockout** is *not* cleared by a session change.

## 0x27 SecurityAccess

- Only available in the **extended** session; in the default session the reply
  is `0x7F` serviceNotSupportedInActiveSession.
- While locked out, every SecurityAccess request replies `0x36` (checked after
  the length and session checks).
- `0x01` requestSeed — length exactly 2.
  - If already unlocked, the seed returned is `0x00000000` and the state is
    unchanged.
  - Otherwise a fresh seed is generated and the state becomes `SEED_SENT`.
- `0x02` sendKey — length exactly 6.
  - Without an outstanding seed: `0x22` conditionsNotCorrect.
  - `key = rotl32(seed XOR 0x5A5A5A5A, 3)` — **XOR first, then rotate**.
  - A wrong key burns the outstanding seed: a new `requestSeed` is required
    before the next attempt.
  - Failed attempts 1 and 2 → `0x35`; attempt 3 → `0x36` plus a 10 s lockout.
  - When the lockout expires the attempt counter resets to 0.
- Any other sub-function → `0x12`.
- **Seed generation** is a deterministic LCG so both implementations produce the
  same sequence: `state = state * 1664525 + 1013904223`, seeded by the
  `rng_seed` argument of `uds_init()`; a generated value of 0 is replaced by 1.
  Use the *same* `rng_seed` on both sides of a differential run.

## 0x22 ReadDataByIdentifier

- Exactly **one** DID per request; length must be exactly 3, else `0x13`.
- `F190` VIN (17 bytes), `F187` part number (10 bytes), `2A01` pad wear
  (4 bytes, % remaining, one per wheel: FL, FR, RL, RR), `2A02` reservoir
  pressure (2 bytes, big endian, 0.01 bar/bit).
- Unknown DID → `0x31`.
- Readable in **both** sessions, with no security requirement.

## 0x2E WriteDataByIdentifier

Check order — this matters for parity:

1. `req_len < 3` → `0x13`
2. session is not extended → `0x7F`
3. security not unlocked → `0x33`
4. DID is not `F187` → `0x31`
5. `req_len != 13` → `0x13`

## 0x14 ClearDiagnosticInformation

- Length must be exactly 4, else `0x13`.
- Only `groupOfDTC == 0xFFFFFF` is accepted; anything else → `0x31`.
- Clears **stored** DTCs: an entry whose status has bit 0 (`testFailed`) set is
  currently present and survives the clear.
- Positive response is the single byte `54`.

## 0x19 ReadDTCInformation

- Sub-function `0x02` reportDTCByStatusMask only, else `0x12`.
  The sub-function is validated **before** the exact length.
- Length must then be exactly 3, else `0x13`.
- Response: `59 02 FF` followed, for every valid DTC with
  `(status & mask) != 0`, by 3 code bytes and the status byte, in slot order.
- A mask that matches nothing yields the 3-byte header with no records — this is
  a positive response, not `0x31`.

## Power-on data (`uds_init`)

| Item | Value |
|------|-------|
| session | default |
| VIN | `WDB9634031L123456` |
| part number | `A000429783` |
| pad wear | 88, 85, 62, 60 |
| reservoir pressure | 820 (= 8.20 bar) |

Default DTCs, in slot order:

| Code | Status | Note |
|------|--------|------|
| `B12004` | `0x2F` | active (testFailed set) |
| `C00512` | `0x08` | stored |
| `D1010A` | `0x28` | stored |
| `912345` | `0x09` | active |
