#pragma once

#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

namespace aero {

// All 30 Yamaha SPX90 algorithm architectures
enum class AlgorithmType : int {
    RevHall = 0,       // #1  REV 1 HALL
    RevRoom,           // #2  REV 2 ROOM
    RevVocal,          // #3  REV 3 VOCAL
    RevPlate,          // #4  REV 4 PLATE
    EarlyRef1,         // #5  EARLY REF 1
    EarlyRef2,         // #6  EARLY REF 2
    DelayLR,           // #7  DELAY L,R
    StereoEcho,        // #8  STEREO ECHO
    StereoFlangeA,     // #9  STEREO FLANGE A
    StereoFlangeB,     // #10 STEREO FLANGE B
    ChorusA,           // #11 CHORUS A
    ChorusB,           // #12 CHORUS B
    StereoPhasing,     // #13 STEREO PHASING
    Tremolo,           // #14 TREMOLO
    Symphonic,         // #15 SYMPHONIC
    GateReverb,        // #16 GATE REVERB
    ReverseGate,       // #17 REVERSE GATE
    AdrNoiseGate,      // #18 ADR / NOISE GATE
    Compressor,        // #19 COMPRESSOR
    ReverbAndGate,     // #20 REVERB & GATE
    PitchChangeA,      // #21 PITCH CHANGE A
    PitchChangeB,      // #22 PITCH CHANGE B
    PitchChangeC,      // #23 PITCH CHANGE C
    PitchChangeD,      // #24 PITCH CHANGE D
    FreezeA,           // #25 FREEZE A
    FreezeB,           // #26 FREEZE B
    AutoPan,           // #27 AUTO PAN
    TriggeredPan,      // #28 TRIGGERED PAN
    DelayVibrato,      // #29 DELAY VIBRATO
    ParametricEQ,      // #30 PARAMETRIC EQ
    Count
};

enum class BitDepthMode : int {
    Linear12Bit = 0,     // 12-bit linear quantization (4096 steps)
    Companded12Bit = 1,  // 12-bit floating / chord companded quantization
    Linear16Bit = 2      // 16-bit linear PCM quantization (65536 steps)
};

enum class RamModel : int {
    Spx90_MkI = 0,       // Original 1985 SPX90 (500 ms max delay / 16K DRAM)
    Spx90_MkII = 1       // 1986 SPX90II (2000 ms max delay / 64K DRAM)
};

enum class ParamFormat : int {
    FloatSeconds,      // e.g. 0.3 - 99.0 s
    FloatMilliseconds, // e.g. 0.1 - 500.0 ms
    FloatHertz,        // e.g. 0.1 - 20.0 Hz
    FloatRatio,        // e.g. 0.1 - 1.0 (High damping)
    FloatRoomSize,     // e.g. 0.1 - 20.0
    IntPercent,        // e.g. 0 - 100 %
    SignedPercent,     // e.g. -99 - +99 %
    IntSemitones,      // e.g. -12 - +12
    IntCents,          // e.g. -100 - +100 cents
    IntDecibels,       // e.g. -15 - +15 dB
    IntLiveness,       // e.g. 0 - 10
    FilterHpf,         // THRU, 32Hz .. 1.0kHz
    FilterLpf,         // 1.0kHz .. 11.0kHz, THRU
    ErType,            // 0=HALL, 1=RANDOM, 2=REVERSE, 3=PLATE
    PanDirection,      // 0=L->R, 1=L<-R, 2=L<->R
    FreezeMode,        // 0=MANUAL, 1=AUTO
    BaseKey,           // 0=OFF, 36..96 (C1..C6)
    OnOff,             // 0=OFF, 1=ON
    FloatQ             // 0.5 - 5.0
};

struct ParamMeta {
    const char* name = "UNUSED";
    const char* shortLcdName = "PARAM";
    float minVal = 0.0f;
    float maxVal = 100.0f;
    float defaultVal = 50.0f;
    float step = 1.0f;
    ParamFormat format = ParamFormat::IntPercent;
    bool active = false;
};

constexpr int kMaxProgramParams = 8;
constexpr int kNumRomPresets = 30;
constexpr int kNumUserPresets = 60;
constexpr int kTotalMemorySlots = kNumRomPresets + kNumUserPresets; // 90 total (1..90)

struct Spx90Program {
    int memoryNumber = 1;                // 1..90
    std::string name = "REV 1 HALL";     // 16-char max for LCD
    std::string category = "REV";
    AlgorithmType algorithm = AlgorithmType::RevHall;
    bool isRom = true;
    std::array<float, kMaxProgramParams> params{}; // 8 algorithm-specific parameters
    float balance = 100.0f;              // 0..100 % wet/dry balance
    float outLevel = 100.0f;             // 0..100 % output level
};

} // namespace aero
