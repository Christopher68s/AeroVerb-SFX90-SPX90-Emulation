# SFX90 / AeroVerb — Audit Report

Scope: the five areas in §6 of `SFX90_Technical_Specification_and_Audit_Brief.md`, plus a spec-vs-code comparison.
Method: full read of `Source/` (processor, editor, DSP engine, converter, all four algorithm files, preset DB, tests, CMake, CI), then a standalone probe compiled against the real DSP headers (MSVC, `/O2`, not part of the repo) to confirm the numeric claims.

**Verified** = reproduced by the probe. **Read** = from code reading only; not run.
No source files were modified.

---

## Summary

| # | Severity | Finding | Status |
|---|----------|---------|--------|
| 1 | High | Converter Drive is applied twice (ADC *and* DAC stage) | Verified |
| 2 | High | Input saturator is non-monotonic: loud signals get quieter | Verified |
| 3 | High | Pitch Change A self-oscillates at high feedback | Verified |
| 4 | High | Saved state omits user RAM #31–#90; restore clobbers Balance/Out Level | Read |
| 5 | Medium | Audio thread takes a mutex, allocates, and notifies the host (MIDI program change, per-block sync) | Read |
| 6 | Medium | Unsynchronised cross-thread access (UI ↔ audio) | Read |
| 7 | Medium | Recall/edit vs. host-automation sync race causes one-block wrong params + spurious "modified" flag | Read |
| 8 | Medium | SPX90II (MkII) LCD shows ×4 times that the DSP does not apply | Read |
| 9 | Medium | Reverb Rev Time above ~20 s has no effect; host tail length is 5 s | Verified |
| 10 | Medium | Smoothing/duck constants are per-sample, not per-second (sample-rate dependent) | Read |
| 11 | Medium | UI repaints everything at 60 Hz, takes the state mutex at 60 Hz, nothing cached | Read |
| 12 | Low | Peak meter decay depends on host buffer size | Read |
| 13 | Low | Comb "modulation" changes the wrap point (1-sample discontinuities) | Read |
| 14 | Low | No NaN/Inf guard on output; restored params unclamped | Read |
| 15 | Low | Hard bypass / mono-input switching without crossfade | Read |
| 16 | Low | MIDI notes are not sample-accurate; Trg Delay > 0 is ignored; lookahead wraps at high clock | Read |
| 17 | Info | Test suite does not cover what the spec says it does; CI artifact paths stale | Read |
| 18 | Info | Spec/README have drifted from the code in many places (table in §Spec drift) | Read |

What passed: see §What is fine.

---

## High

### 1. Converter Drive is applied at both the ADC and the DAC stage
`Source/DSP/ConverterEngine.h:269-277`. `quantizeSample()` (lines 187-226) does drive → soft-saturate → add noise → quantise. It is called for the A/D step (269-270) **and again for the D/A step** (276-277), so drive, saturation, dither and the overflow flag all run twice on the wet path.

Probe, identity DSP tick, 1 kHz, small signal:

| Converter Drive | Wet gain measured |
|---|---|
| 0 dB | -0.01 dB |
| +6 dB | **+11.93 dB** |
| +12 dB | **+23.11 dB** |
| +18 dB | +28.21 dB (saturating) |

Dry level is unaffected, so Balance and the apparent character change with the drive knob far more than intended. At default (0 dB) it is harmless apart from a second pass of soft saturation.
**Fix:** split into `adcStage()` (drive + saturate + dither + quantise) and `dacStage()` (quantise, optionally output noise only). Drive and the overflow flag belong to the ADC stage only.

### 2. The soft saturator folds back above |d| ≈ 2.13
`ConverterEngine.h:196`: `sat = d / (1 + 0.22 d²)`. This peaks at d = 2.13 (value 1.07) and then falls toward zero, so the clamp that follows does not make it a limiter.

| input after drive | 0.5 | 1.0 | 2.0 | 3.0 | 4.0 | 8.0 | 16.0 |
|---|---|---|---|---|---|---|---|
| output | 0.474 | 0.820 | 1.000 | 1.000 | **0.885** | **0.531** | **0.279** |

With Drive at +12/+18 dB (and Input up to ×1.33) ordinary program material lands in the fold-back region. Combined with #1 the effect is worse.
**Fix:** use a monotonic curve (`tanh`, `x / sqrt(1 + x²)`, or the cubic knee the spec describes: clean to 0.85, knee above, ceiling ±1.25) and keep the hard clamp after it.

### 3. Pitch Change A feedback is not stable at high settings
`Source/DSP/PitchAlgorithms.h:119-127`. The feedback signal is taken *after* `toneFilter_` (a +1.8 dB peaking EQ at 1.8 kHz, ≈ ×1.23). With `fbGain = 99 × 0.009 = 0.891`, loop gain at the peak is ≈ 1.09. By calculation instability starts around feedback ≈ 90%.

Probe, semi = 0, fine = 0, 200 ms burst then silence, peak per second of tail:
- fb = 0: `0.362 0.000 0.000 0.000 0.000 0.000`
- fb = 99: `1.010 1.014 1.013 1.012 1.013 1.013` — sustained full-scale oscillation, held only by the DRAM clamp.

Pitch D (feedback taken before any filter, loop gain 0.891) is stable.
**Fix:** tap the feedback before `toneFilter_`, or scale the feedback so the worst-case loop gain stays < 1 (including the peak boost).

### 4. Session state is incomplete and restore overwrites values
`Source/PluginProcessor.cpp:667-697`.
- `getStateInformation` stores APVTS, `activeSlotIndex` and 8 `workParam*` values only. **User RAM #31–#90 (names and parameters) are never serialised**, contradicting spec §4.3. Anything saved with STORE is lost when the session reloads.
- `setStateInformation` calls `recallSlotDirectly(slot)` after `replaceState`, which pushes the *slot's* Balance/Out Level into APVTS, overwriting the values the host just restored.
- Restored `workParam*` values are written without clamping to the algorithm's min/max (feeds #14).
**Fix:** serialise `memoryBank_[30..89]` as child elements; restore the bank first, then APVTS, then the working params (clamped); don't let recall overwrite Balance/Out Level on restore.

---

## Medium

### 5. Audio thread locks, allocates and notifies the host
- `processBlock` → `syncDspFromState()` takes `stateMutex_` every block (`PluginProcessor.cpp:204`). The UI takes the same mutex at 60 Hz (`getLcdLines`, `getCurrentActiveProgramCopy`, both copy `std::string`s), so the audio thread can block behind the message thread.
- A MIDI Program Change inside `processBlock` (`:236-241`) calls `recallSlotDirectly` → mutex, `setValueNotifyingHost` ×2, `pushWorkingParamsToApvts` which builds `juce::String("param"+n)` ×8 and does string-keyed `getParameter` lookups (`:295-315`, `:146-166`). That is heap allocation plus host notification on the audio thread.
- `syncDspFromState` does ~25 string-keyed `getRawParameterValue` lookups per block.

The DSP engine itself is clean: buffers are sized in constructors, `processSampleFrame` takes a template lambda (no `std::function`), and `Spx90Program` strings stay within SSO (names are capped at 15 chars).
**Fix:** cache `std::atomic<float>*` pointers in the constructor; replace the mutex on the audio path with a lock-free hand-off (SPSC queue of "program snapshot" messages, or a double buffer); do MIDI recall via an `AsyncUpdater`/message-thread call and send the snapshot to the engine through the queue.

### 6. Unsynchronised cross-thread access
- `getActiveSlotIndex()`, `getPreviewSlotIndex()`, `isProgramModified()`, `getPanelMode()`, `getSelectedParamIndex()`, `getCurrentProgram()` read plain ints/enums without the lock (`PluginProcessor.h:68-72, 85-86`, `.cpp:104`). `syncDspFromState` writes `currentParamCursor_` / `programModified_` from the audio thread.
- `lastMidiNoteReceived_` is written on the audio thread (`:233`) and read on the UI thread (`:399, 406`).
- `pressMidiTrigger()` (UI thread, `:393-407`) calls `dspEngine_.handleMidiNoteOn()`, which writes `midiTriggered_`, `lastMidiNote_` and `activeMidiNote_` inside the engines while `processBlock` reads them. In `EqPanFreezeEngine::processTick` the flag is cleared only when that engine runs, so a trigger set while another algorithm is active survives and fires later.
**Fix:** make those members `std::atomic` (relaxed is fine) or route the trigger through the same lock-free queue.

### 7. Recall / edit vs. automation sync can apply the wrong params for a block
`pushWorkingParamsToApvts` updates `lastPushedNormParams_` under the lock, releases it, and only then calls `setValueNotifyingHost` (`:146-166`). If a block runs in between, `syncDspFromState` sees `rawNorm != lastPushed` (`:208`), treats it as host automation, and writes the **old** normalised value into `workingProgram_` using the **new** algorithm's min/max range. It also sets `programModified_ = true` and moves the param cursor. It self-corrects on the next block, but:
- one block of wrong parameters reaches `pendingProgram_`/`currentProgram_`, which matters if the duck completes within that block (large buffers),
- the asterisk/"modified" state and cursor jump spuriously.
Related: `Spx90DspEngine::setActiveProgram` is fed a full `Spx90Program` every block, so `currentProgram_ = program` copies every block (cheap, but there is no "only continuous params changed" fast path), and STORE changes `memoryNumber`, which triggers a needless duck + tank clear (`Spx90DspEngine.cpp:72-75`).
**Fix:** apply APVTS changes via `ParameterListener`/atomics with a generation counter, or update APVTS first and `lastPushed` after; compare algorithm *only* (not `memoryNumber`) to decide on ducking.

### 8. MkII display disagrees with the DSP for many parameters
`Spx90PresetDatabase.cpp:384-394` multiplies every `FloatMilliseconds` value whose `maxVal ≥ 250` by 4 on SPX90II. The DSP applies ×4 only to Delay L,R / Stereo Echo (`ModDelayAlgorithms.h:357-361`) and Freeze start/end (`EqPanFreezeAlgorithms.h:255-273`). On MkII the LCD and Studio knobs therefore show 4× the real time for: ER/Gate pre-delay, Hold/Release (#16, #17, #20), ADR Attack/Hold/Release (#18), Compressor Release (#19), Pitch delays (#21-#23), Triggered Pan (#28), Vibrato delay (#29).
**Fix:** put a `scalesWithRam` flag in `ParamMeta` and use it for both the formatter and the DSP.

### 9. Rev Time is effectively capped; host tail is 5 s
`ReverbAlgorithms.h:177-180` clamps comb gain to 0.985. Probe, REV 1 HALL, level relative to the first 0.5 s:

| RevTime param | t = 1 s | 6 s | 10 s | 20 s | 30 s |
|---|---|---|---|---|---|
| 2 | -28.0 | -60.4 | -61.9 | -63.6 | -64.4 |
| 10 | -4.3 | -34.0 | -50.3 | -55.1 | -57.6 |
| 30 | -0.6 | -13.2 | -22.5 | -43.3 | -49.6 |
| **99** | **-0.6** | **-13.2** | **-22.5** | **-43.3** | **-49.6** |

30 s and 99 s are identical, so the 0.3–99 s range is mostly dead above ~17–20 s. Separately `getTailLengthSeconds()` returns 5.0 (`PluginProcessor.cpp:98`), so a host may stop calling `processBlock` before long tails finish.
**Fix:** raise the gain clamp (e.g. 0.9995) or scale `damp` so long times are reachable (watch float precision), and report a tail based on the current Rev Time.

### 10. Smoothing and recall duck are per-sample constants
`Spx90DspEngine.cpp:222-223`: `kSmoothCoeff = 0.004`, `kFadeStep = 0.0032` per sample. Time constants scale with host rate: ≈ 5.7 ms / 7 ms at 44.1 kHz, ≈ 1.3 ms / 1.6 ms at 192 kHz, so a recall at 192 kHz is a 1.6 ms duck (audible click on low-frequency content). The spec itself is inconsistent (§3.4 says ~5 ms, the diagram says ~10 ms).
**Fix:** derive coefficients from `hostSampleRate` in `prepare()`.

### 11. Editor repaint cost and mutex traffic
`PluginEditor.cpp:575-617` — every 60 Hz tick: copies the program (mutex + two strings), sets and re-formats text on 8 sliders, then calls `repaint()` on the whole 1760×192 (or ×582) component unconditionally. There is no `setBufferedToImage`/cached faceplate (`grep` finds none), and `drawManufacturerBadge()` (`:947-1065`) rebuilds `GlyphArrangement`s and `Path`s on every paint. On high-DPI displays this is the main idle CPU cost, and it competes with the audio thread for `stateMutex_` (#5).
**Fix:** cache the static faceplate/badge into a `juce::Image` at the current scale factor and invalidate on scale change; track dirty state (LCD lines, meter segments, blink phase, drawer animation) and call `repaint(rect)` only for changed regions; skip slider updates when values are unchanged; read editor state from atomics/a snapshot instead of locking.
Badge accuracy: it is drawn from vector paths, so it scales cleanly, but the `×1.19` font-height factor and glyph-advance maths are hand-tuned. I have **not** rendered it at several DPI scales, so "matches the CSS at all scale factors" is unverified.

---

## Low

12. **Peak meter** (`Spx90DspEngine.cpp:285-288`) decays by ×0.88 *per block*. At 64-sample buffers the UI (60 Hz) sees about 11 decays between polls and under-reads; at 2048 it holds far longer. Use a time-based decay and a "max since last read" value.
13. **Comb modulation** (`ReverbAlgorithms.h:223-233`) changes the buffer wrap length by ±3 samples instead of moving a fractional read pointer, so each LFO step makes a 1-sample discontinuity inside the recirculating loop.
14. **No non-finite guard.** `std::clamp(NaN, …)` returns NaN, so a single NaN in a DRAM or comb loop never clears. No path produced NaN in testing (see §What is fine), but restored `workParam*` values are unclamped (#4). Add a cheap `isfinite` check at the output of `processBlock` that triggers `reset()`.
15. **Hard switches.** Bypass (`Spx90DspEngine.cpp:257-261`), mono-input and the "filter on if > 25 Hz / < 11.5 kHz" thresholds switch abruptly. Bypass also skips the converter and tanks, so tails are frozen and resume stale.
16. **MIDI timing and trigger delay.** Note-ons and program changes act at the start of the block, ignoring `metadata.samplePosition`. Positive `Trg Delay` is ignored (`-trgDly` clamped to 0..100 in ADR/Compressor). The 4096-sample lookahead wraps if the clock is overclocked (100 ms × 48 kHz = 4800 > 4095).
- Also noted: **Hi-Fi Filter Bypass** (`ConverterEngine.h:187-188, 234-253`) disables quantisation, drive and noise as well as the filters; the name and the spec only mention filters.

---

## Info

### Tests and CI
`Source/Tests/DspVerificationMain.cpp` contains: 1 filter-bandwidth check, 30 ROM-preset smoke tests, 1 Pitch B vs C check (= 32). The spec lists additional coverage that does not exist: REV 1 RT60 progression, Symphonic decorrelation, MkI/MkII ceiling, 12-bit companded vs 16-bit steps, multi-chain stress, state persistence. Runs at 48 kHz only, no extreme parameters, and `CMakeLists.txt` has no `enable_testing()/add_test()`. The suite passes with bugs #1-#3 present.
`.github/workflows/build.yml:28-29, 48-50` upload `Aero FX90.vst3/.exe/.app`, but `PRODUCT_NAME` is `SFX90`, so the artifact paths match nothing.

### Spec drift (spec/README vs code)
| Topic | Spec says | Code |
|---|---|---|
| Anti-alias / reconstruction | 14 kHz, 4th-order Butterworth (Q 0.5412 / 1.3065) | 12 kHz, 6th-order, Q 0.54 / 0.80 / 2.15 (`ConverterEngine.h:311`; Q 2.15 is resonant, not Butterworth) |
| DC blocker | 18 Hz | 20 Hz |
| Reconstruction | 70 % Hermite + 30 % ZOH, +0.6 dB shelf at 11.5 kHz | 65 % ZOH + 35 % linear, no shelf |
| Fixed point | `FixedPointUtils.h`, Q1.23 accumulators, Q1.15 DRAM | no such file; float math, rounding only at DRAM/converter |
| Algorithms | 28 types, Symphonic = #14 | 30 enum entries, Symphonic = #15 |
| Phaser | 8/12-stage | 6-stage |
| LFO resolution | 12-bit (÷2048) | ÷512 (10-bit) |
| File names | `DelayMemoryBuffer.h`, `ModulationAlgorithms.h`, `PitchDelayGateAlgorithms.h`, `Source/Presets/…`, `Tests/` | `DramBuffer.h`, `ModDelayAlgorithms.h`, `PitchAlgorithms.h`+`EqPanFreezeAlgorithms.h`, `Source/DSP/Spx90PresetDatabase.*`, `Source/Tests/` |
| Manufacturer code | `DSIn` | `DSym` (`CMakeLists.txt:17`) |
| Targets | `AeroFX90_VST3`, `AeroFX90_Standalone` | `AeroFX90` plugin target |
| Persistence | all 90 slots saved | not saved (#4) |
| Window | 1760×192 / 1760×582 | matches code; `README.md:24` still says 1420×260 |
| `Docs/aero_fx90_specification.md` | – | older document ("Aero FX90 / AF90", 12 kHz, planned workspace in `.gemini`); superseded by the brief |

---

## What is fine
- **Sample-rate / clock matrix** (Read + Verified): all 30 ROM presets × {as-is, all-min, all-max} params × host 44.1/48/96/192 kHz × clock 15/31.25/48 kHz, with Companded 12-bit, MkII, +18 dB drive, noise floor -50 dB, MIDI trigger: 1,080 cases, **no NaN/Inf**, worst peak 2.44 (some of that is bug #1), CPU 0.6-2.1 % of real time in a single-thread release build.
  When the clock exceeds the host rate the phase accumulator runs up to 2 ticks per host sample and keeps correct timing; it simply decimates without a filter (expected aliasing, no crash).
- **Feedback stability:** Delay L,R / Stereo Echo (≤ 0.94 loop gain, damping LP has unity DC gain, cross-coupling matrix row-sum 1), Flanger (≤ 0.94), Phaser (all-pass cascade, |a| < 1, ≤ 0.84), reverb combs (≤ 0.985), ER liveness feedback (probe at Liveness 10 / Room 20 decays: peaks 0.402→0.001 over 6 s). All are also bounded by clamps.
- **Denormals:** `ScopedNoDenormals` is set at the top of `processBlock`; comb/all-pass/DRAM storage is rounded to 12/16-bit steps so it cannot hold subnormal values; filter and LP states depend on FTZ, which is covered on the audio thread.
- **DRAM addressing:** power-of-two mask, read delay clamped to 1…62500 samples; no out-of-range access found.
- **Window sizing:** `setResizable(false, false)` and 60 Hz drawer animation as described.

Not examined: AU/macOS build, VST3 validator run in a host, real-time behaviour under an actual DAW, 192 kHz float32 biquad noise at 20 Hz (suspected, not measured).
