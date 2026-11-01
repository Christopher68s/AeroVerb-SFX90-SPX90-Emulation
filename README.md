# Digital Symphony Instruments — AeroVerb SFX90 (`SFX90`)

Faithful JUCE/C++ emulation of the **1985 Yamaha SPX90 / SPX90II Digital Multi-Effects Processor** (`YM3804` DSP + `YM3807` 16-Channel LFO Generator + `31.25 kHz` Converter Engine).

- **Workspace Location:** `D:\Pictures\VST-Web Instrument  Builds\SPX-90 Emulation`

## Key Features
- **31.25 kHz Internal DSP Engine:** True downsampling/upsampling with steep 6th-order ~12 kHz anti-aliasing and reconstruction filters (20 Hz – 12 kHz hardware bandwidth).
- **3-Way Bit Depth & Converter Lab:**
  - `12-Bit Linear` (4096-step early digital grit)
  - `12-Bit Companded` (floating chord quantization)
  - `16-Bit Linear PCM` (Sony CX20018 / Burr-Brown PCM53JP path)
  - Adjustable **ADC Drive**, **Noise Floor Trim**, **DSP Clock Rate** (15–48 kHz), and **Hi-Fi Filter Bypass**.
- **SPX90 MkI & SPX90II (2x DRAM) Switch:** Toggles between the 1985 500 ms DRAM buffer and the 1986 SPX90II 2000 ms extended DRAM buffer.
- **Complete 30 Factory ROM Presets + 60 User RAM Slots (Memories 01–90):**
  - **Reverbs (`REV 1–4`):** Hall, Room, Vocal, Plate (with 2-stage input diffusion & stereo comb cross-coupling)
  - **Early Reflections (`EARLY REF 1–2`):** Hall, Random, Reverse, Plate geometries (24-tap prime reflection cluster)
  - **Delays (`DELAY L,R`, `STEREO ECHO`)**
  - **Modulation (`SYMPHONIC`, `CHORUS A/B`, `STEREO FLANGE A/B`, `STEREO PHASING`, `TREMOLO`, `DELAY VIBRATO`)**
  - **Gate & Dynamics (`GATE REVERB`, `REVERSE GATE`, `REVERB & GATE`, `ADR-NOISE GATE`, `COMPRESSOR`)**
  - **Pitch Change (`PITCH CHANGE A/B/C/D`):** Dual-pointer circular splice pitch shifting with authentic latency, splice artifacts, `Pitch B` mono dual-voice, `Pitch C` stereo ±8 cent detune, and MIDI note tracking
  - **Freeze (`FREEZE A/B`), Pan (`AUTO PAN`, `TRIGGERED PAN`), and `PARAMETRIC EQ`**
- **Dual-Mode Fixed-Size GUI (`1760 px` Wide):**
  - **Authentic 1985 1U Rack Panel (`1760 × 192 px`):** Glossy Studio Retro-Fi faceplate (cached to an image at the current display scale), smoked-acrylic display window (`AeroVerb / SFX90`), slanted red 7-segment LED memory display, 5×7 dot-matrix LCD, 7-bar LED input meter, Digital Symphony Instruments `.mfr` logo, and tactile sculpted push-buttons (`#1~#30 READ ONLY` protection when storing to ROM).
  - **Animated `STUDIO VIEW ▼ / ▲` Console Drawer (`1760 × 582 px`):** Smooth 60 FPS gliding drawer with 8 auto-binding, DAW-automatable parameter rotary knobs (`param1`–`param8`), converter controls, and the **Multi-Chain Routing Matrix** (`MOD -> PITCH -> DELAY -> REVERB`).
- **Real-time safe audio thread:** cached parameter pointers, no allocation or blocking locks in `processBlock` (`try_lock` + snapshot fallback), MIDI Program Change applied via `AsyncUpdater`, sample-rate-independent smoothing / duck / meter decay, NaN/Inf output guard, and a host tail length that follows the current Rev Time.
- **Session persistence:** APVTS + active slot + edited working params + all 60 User RAM slots (`#31–#90`).

## Building Locally
Double-click `Build_and_Install_SFX90.bat`, or run from a Developer Command Prompt:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Build Outputs
- **VST3 Plugin:** `build/AeroFX90_artefacts/Release/VST3/SFX90.vst3`
- **Installed DAW VST3:** `%LOCALAPPDATA%/Programs/Common/VST3/SFX90.vst3`
- **Standalone App:** `build/AeroFX90_artefacts/Release/Standalone/SFX90.exe`
- **Automated DSP Test Runner:** `build/AeroFX90_Tests.exe`
