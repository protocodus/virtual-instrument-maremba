# Sanitized 96 kHz Main calibration time budget

The [original sanitizer job](https://github.com/protocodus/virtual-instrument-maremba/actions/runs/39678993803/job/119496216577) at source commit `78fb821254a31b88cf85b4d515a734fb4d471140` **failed**. It completed on 2026-10-21 at 22:36 UTC with 77/78 selected tests passing. Its only failure was the 1800-second CTest limit on `Maremba.MainCalibration96000`; no ASan or UBSan diagnostic was observed in the retained full log. This record does not turn that timeout into a runtime pass.

| Main calibration rate | Original sanitizer result |
| --- | --- |
| 44100 Hz | Passed, 857.01 s |
| 48000 Hz | Passed, 883.69 s |
| 96000 Hz | Timeout, 1800.03 s |

The 96 kHz output contains 70 complete CSV rows of the required 72, ending at the normal phrase for Kalimba Thumb Lullaby. The final two renders did not report completion. The workflow runs ASan + UBSan in RelWithDebInfo with CTest parallelism four; its job limit remains 120 minutes. Late progress and the other two rate results support a bounded extra wall-time allowance for this instrumented workload, rather than a change to any audio acceptance criterion.

The CMake change raises only the sanitized 96000 Hz registration to 3600 seconds. Sanitized 44100/48000 Hz and every non-sanitized Main calibration registration remain at 1800 seconds. Each command still uses `MarembaMainCalibrationTests --rate RATE --stress`: 24 states × 3 scenarios × 3 rates = 216 required renders. No render, RMS/peak reference, numerical threshold, stop/stem/allocation gate, sanitizer option, or workflow setting changed. The one-hour limit is a hang guard, not a guarantee of completion on every shared runner.

## Configure-only verification

Fresh isolated build directories were configured before and after the edit, with sanitizer mode ON and OFF. No contract target was compiled or executed. `configure_registration.py` records the exact configure and CTest show-only argument arrays; the generated CTest files retain executable commands even though those executables were not built. Plugin/tools/universal builds and Python checks were disabled for this local registration proof; the extra sanitizer fuzz target explains the 78 versus 77 registration counts.

`verify_registration.py` compares every generated command and every CTest property reported by show-only JSON (excluding source backtrace metadata). It found precisely one change: sanitized `Maremba.MainCalibration96000` TIMEOUT 1800 → 3600. All commands, labels and other properties match before/after. See `registration-verification.json` and the four captured configure/show-only results.

Commands executed from the repository root:

```sh
python3 Docs/validation/ci-sanitizer-maincalibration-timeout-2026-10-22/configure_registration.py before ON
python3 Docs/validation/ci-sanitizer-maincalibration-timeout-2026-10-22/configure_registration.py before OFF
# Apply the scoped CMake timeout edit.
python3 Docs/validation/ci-sanitizer-maincalibration-timeout-2026-10-22/configure_registration.py after ON
python3 Docs/validation/ci-sanitizer-maincalibration-timeout-2026-10-22/configure_registration.py after OFF
python3 Docs/validation/ci-sanitizer-maincalibration-timeout-2026-10-22/verify_registration.py
```

`Source`, `Tests`, and `.github/workflows/ci.yml` match the original commit, including the production source tree `5abd33ee09dcbc0d46fdb18753698bfcf2f8eb57` and test tree `fb9d8991ff304593ed36c7532e1a08372675b14b`. This test-configuration change provides no reason to rebuild an audio payload; earlier SDK build and installation receipts retain their original source SHAs. Fresh hosted CI must still establish an actual sanitizer pass.

`job.log` is the original 69,091-byte log, preserved without newline conversion (`-text`), SHA-256 `5bd0ed8f172a5a61d15c6b7789ad5686bd8ad66c23bd6eda9ba8a2bc7732bc25`. `receipt.json` records its original failure status and capture scope.
