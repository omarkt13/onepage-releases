# Review pass 6: test coverage and bench test plan (area f)

Scope: untested logic in `firmware/src/core` and `app/OnePageKit`, and a bench
test plan in `docs/TESTING.md`. Ranked by severity.

## Findings

| # | Severity | File | Problem | Fix | State |
|---|---|---|---|---|---|
| M1 | Medium | `firmware/src/ui/screens.c:331`, `:391` | The Calendar list started at `cal_first_current()` and showed every later event. Events are in start order, so while an all-day event runs, events that ended earlier today still showed. The navigation count (`cal_current_count()`) was smaller than the rows drawn, so the last events could not be selected, and the detail screen could open the wrong event | New `cal_current_nth()` in `src/core/calendar.c`. The list and the detail screen use it, so both skip ended events. Test `cal_running_all_day` | Fixed |
| M2 | Medium | `firmware/src/main.c:51` | A new image was confirmed even when `ble_init()` failed. MCUboot then keeps an image that cannot receive another update over Bluetooth, and the rollback in firmware spec section 9 never happens | Confirm only when Bluetooth started. Otherwise log `new image not confirmed`, restart, and let MCUboot return to the previous image. Bench test T-UPD-6 | Fixed |
| M3 | Medium | (missing) `shared/tools/` | Nothing could make the signed manifest the app requires (Ed25519 over `version\nsize\nsha256`). The update tests could not run, and the app's signature format had no check against a signer | New `shared/tools/make_manifest.py` (`keygen`, `sign`, `verify`, `test-fixture`). New fixture `manifest-test.json` and Swift test `testManifestSignatureFromReleaseTool` check the app's `signedMessage` against it. Sign and verify, and refusal with a wrong key, checked here with Python | Fixed |
| M4 | Medium | `firmware/src/core/privacy.h`, `src/app.c` | The 7-day erase and the restart-while-away rule could not be checked on the bench without waiting 7 days | Bench-only Kconfig `ONEPAGE_TEST_AWAY_ERASE_MIN` replaces 7 days with N minutes and saves the time away every minute. CMake prints `BENCH BUILD` when it is set. Default builds do not change (checked: no definition in the default build, `600000ull` in a 10-minute build) | Fixed |
| L1 | Low | `firmware/src/core/calendar.c` `cal_erase()` | Erase cleared the stored calendar but left a partial sync's text in the staging buffer in RAM | `cal_erase()` also clears the staging buffer. Test `cal_running_all_day` | Fixed |
| L2 | Low | `app/OnePageKit/Sources/OnePageKit/Transfer.swift` `apply(deviceOffset:)` | `confirmedOffset` kept the highest offset ever reported. Since pass 3 a new START can make the device discard a partial book and report 0, so the progress bar showed a wrong value | `confirmedOffset` follows the device's offset. Test `testPlanFollowsDeviceWhenPartialBookIsDiscarded` | Fixed |
| L3 | Low | `firmware/tests/host` | No tests for: damaged calendar blobs (truncation at every byte, end before start, long title, count over 100, trailing bytes), damaged `.opb` files, reader paging past its 32-page history, long words, UTF-8 edge cases and copying without splitting a character, schedule after disconnect, `op_xfer_result_encode`, `op_filter_simple_reply`, 12-hour midnight and noon, long date | New suite `test_coverage` (7 cases, 464 checks) | Fixed |
| L4 | Low | `app/OnePageKit/Tests` | No tests for: manifest decoding with unknown and missing keys, hardware filter, damaged device replies (truncated, unknown status, wrong version, unknown opcode), FAST and LIST command bytes | New `CoverageTests.swift`. Not run here (no Swift compiler) | Fixed, not run |
| L5 | Low | `docs/` | No bench test plan | New `docs/TESTING.md`: app first-build checklist (A1–A8), 62 numbered tests with setup, steps and pass criteria, the measurement table from firmware spec section 11 step 4, and a test for each open bench item in `STATUS.md` (items 1, 2, 3, 9, 11, 12, 13) | Fixed |

## Checks

| Check | Result |
|---|---|
| Host tests | 3 of 3 suites pass: vectors, behaviour (10 cases, 247 checks), coverage (7 cases, 464 checks). AddressSanitizer and UBSan on |
| `nrf54l15dk/nrf54l15/cpuapp` with MCUboot | Pass. App 394 KB flash, 223 KB RAM (85.2%). No warnings from `firmware/src` |
| `onepage_mini/nrf54l15/cpuapp` with MCUboot | Pass. No warnings from `firmware/src` |
| Bench build `-Dfirmware_CONFIG_ONEPAGE_TEST_AWAY_ERASE_MIN=10` | Pass. Prints `BENCH BUILD`. Defines `PRIV_ERASE_DELAY_MS=600000ull` |
| `vectors.json` regenerates | No difference |
| `make_manifest.py` | `test-fixture`, `keygen`, `sign`, `verify` OK. A wrong key gives `signature: FAIL` |
| Swift | Not compiled (no Swift compiler). Steps A1–A5 in `docs/TESTING.md` |

## Result

0 high, 4 medium, 5 low. All 9 fixed. The pass is not clean (medium findings).
