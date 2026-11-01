#include "Spx90PresetDatabase.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace aero {

namespace {

using MetaArray = std::array<ParamMeta, kMaxProgramParams>;

// Helper to build an 8-element ParamMeta array
MetaArray makeMeta(std::initializer_list<ParamMeta> list) {
    MetaArray arr{};
    size_t i = 0;
    for (const auto& item : list) {
        if (i < kMaxProgramParams) {
            arr[i++] = item;
        }
    }
    return arr;
}

const std::array<MetaArray, static_cast<size_t>(AlgorithmType::Count)> kAlgMetas = {{
    // 0: RevHall (#1 REV 1 HALL)
    makeMeta({
        {"Rev Time",   "REV TIME", 0.3f,  99.0f, 2.6f,   0.1f, ParamFormat::FloatSeconds,      true},
        {"High Damp",  "HIGH",     0.1f,  1.0f,  0.6f,   0.1f, ParamFormat::FloatRatio,        true},
        {"Pre-Delay",  "DELAY",    0.1f,  50.0f, 30.0f,  0.1f, ParamFormat::FloatMilliseconds, true},
        {"HPF Cutoff", "HPF",      20.0f, 1000.f,20.0f,  10.f, ParamFormat::FilterHpf,         true},
        {"LPF Cutoff", "LPF",      1000.f,12000.f,8000.f,500.f,ParamFormat::FilterLpf,         true},
    }),
    // 1: RevRoom (#2 REV 2 ROOM)
    makeMeta({
        {"Rev Time",   "REV TIME", 0.3f,  99.0f, 1.5f,   0.1f, ParamFormat::FloatSeconds,      true},
        {"High Damp",  "HIGH",     0.1f,  1.0f,  0.7f,   0.1f, ParamFormat::FloatRatio,        true},
        {"Pre-Delay",  "DELAY",    0.1f,  50.0f, 30.0f,  0.1f, ParamFormat::FloatMilliseconds, true},
        {"HPF Cutoff", "HPF",      20.0f, 1000.f,20.0f,  10.f, ParamFormat::FilterHpf,         true},
        {"LPF Cutoff", "LPF",      1000.f,12000.f,8000.f,500.f,ParamFormat::FilterLpf,         true},
    }),
    // 2: RevVocal (#3 REV 3 VOCAL)
    makeMeta({
        {"Rev Time",   "REV TIME", 0.3f,  99.0f, 2.4f,   0.1f, ParamFormat::FloatSeconds,      true},
        {"High Damp",  "HIGH",     0.1f,  1.0f,  0.5f,   0.1f, ParamFormat::FloatRatio,        true},
        {"Pre-Delay",  "DELAY",    0.1f,  50.0f, 45.0f,  0.1f, ParamFormat::FloatMilliseconds, true},
        {"HPF Cutoff", "HPF",      20.0f, 1000.f,80.0f,  10.f, ParamFormat::FilterHpf,         true},
        {"LPF Cutoff", "LPF",      1000.f,12000.f,8000.f,500.f,ParamFormat::FilterLpf,         true},
    }),
    // 3: RevPlate (#4 REV 4 PLATE)
    makeMeta({
        {"Rev Time",   "REV TIME", 0.3f,  99.0f, 1.8f,   0.1f, ParamFormat::FloatSeconds,      true},
        {"High Damp",  "HIGH",     0.1f,  1.0f,  0.7f,   0.1f, ParamFormat::FloatRatio,        true},
        {"Pre-Delay",  "DELAY",    0.1f,  50.0f, 20.0f,  0.1f, ParamFormat::FloatMilliseconds, true},
        {"HPF Cutoff", "HPF",      20.0f, 1000.f,20.0f,  10.f, ParamFormat::FilterHpf,         true},
        {"LPF Cutoff", "LPF",      1000.f,12000.f,8000.f,500.f,ParamFormat::FilterLpf,         true},
    }),
    // 4: EarlyRef1 (#5 EARLY REF 1)
    makeMeta({
        {"ER Type",    "TYPE",     0.0f,  3.0f,  0.0f,   1.0f, ParamFormat::ErType,            true},
        {"Room Size",  "ROOM SZ",  0.1f,  20.0f, 2.0f,   0.1f, ParamFormat::FloatRoomSize,     true},
        {"Liveness",   "LIVENESS", 0.0f,  10.0f, 5.0f,   1.0f, ParamFormat::IntLiveness,       true},
        {"Pre-Delay",  "DELAY",    0.1f,  400.0f,10.0f,  0.5f, ParamFormat::FloatMilliseconds, true},
        {"HPF Cutoff", "HPF",      20.0f, 1000.f,40.0f,  10.f, ParamFormat::FilterHpf,         true},
        {"LPF Cutoff", "LPF",      1000.f,12000.f,10000.f,500.f,ParamFormat::FilterLpf,        true},
    }),
    // 5: EarlyRef2 (#6 EARLY REF 2)
    makeMeta({
        {"ER Type",    "TYPE",     0.0f,  3.0f,  2.0f,   1.0f, ParamFormat::ErType,            true},
        {"Room Size",  "ROOM SZ",  0.1f,  20.0f, 2.0f,   0.1f, ParamFormat::FloatRoomSize,     true},
        {"Liveness",   "LIVENESS", 0.0f,  10.0f, 5.0f,   1.0f, ParamFormat::IntLiveness,       true},
        {"Pre-Delay",  "DELAY",    0.1f,  400.0f,10.0f,  0.5f, ParamFormat::FloatMilliseconds, true},
        {"HPF Cutoff", "HPF",      20.0f, 1000.f,20.0f,  10.f, ParamFormat::FilterHpf,         true},
        {"LPF Cutoff", "LPF",      1000.f,12000.f,12000.f,500.f,ParamFormat::FilterLpf,        true},
    }),
    // 6: DelayLR (#7 DELAY L,R)
    makeMeta({
        {"Lch Delay",  "Lch DLY",  0.1f,  500.0f,100.0f, 1.0f, ParamFormat::FloatMilliseconds, true, true},
        {"Lch F.B.",   "Lch F.B.", -99.f, 99.0f, 0.0f,   1.0f, ParamFormat::SignedPercent,     true},
        {"Rch Delay",  "Rch DLY",  0.1f,  500.0f,200.0f, 1.0f, ParamFormat::FloatMilliseconds, true, true},
        {"Rch F.B.",   "Rch F.B.", -99.f, 99.0f, 0.0f,   1.0f, ParamFormat::SignedPercent,     true},
        {"High Damp",  "HIGH",     0.1f,  1.0f,  1.0f,   0.1f, ParamFormat::FloatRatio,        true},
    }),
    // 7: StereoEcho (#8 STEREO ECHO)
    makeMeta({
        {"Lch Delay",  "Lch DLY",  0.1f,  250.0f,170.0f, 1.0f, ParamFormat::FloatMilliseconds, true, true},
        {"Lch F.B.",   "Lch F.B.", -99.f, 99.0f, 50.0f,  1.0f, ParamFormat::SignedPercent,     true},
        {"Rch Delay",  "Rch DLY",  0.1f,  250.0f,170.0f, 1.0f, ParamFormat::FloatMilliseconds, true, true},
        {"Rch F.B.",   "Rch F.B.", -99.f, 99.0f, 50.0f,  1.0f, ParamFormat::SignedPercent,     true},
        {"High Damp",  "HIGH",     0.1f,  1.0f,  0.8f,   0.1f, ParamFormat::FloatRatio,        true},
    }),
    // 8: StereoFlangeA (#9 STEREO FLANGE A)
    makeMeta({
        {"Mod Freq",   "MOD FRQ",  0.1f,  20.0f, 2.5f,   0.1f, ParamFormat::FloatHertz,        true},
        {"Mod Depth",  "MOD DPTH", 0.0f,  100.0f,50.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Mod Delay",  "MOD DLY",  0.1f,  50.0f, 1.2f,   0.1f, ParamFormat::FloatMilliseconds, true},
        {"F.B. Gain",  "F.B.GAIN", -99.f, 99.0f, 35.0f,  1.0f, ParamFormat::SignedPercent,     true},
    }),
    // 9: StereoFlangeB (#10 STEREO FLANGE B)
    makeMeta({
        {"Mod Freq",   "MOD FRQ",  0.1f,  20.0f, 0.5f,   0.1f, ParamFormat::FloatHertz,        true},
        {"Mod Depth",  "MOD DPTH", 0.0f,  100.0f,60.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Mod Delay",  "MOD DLY",  0.1f,  50.0f, 2.0f,   0.1f, ParamFormat::FloatMilliseconds, true},
        {"F.B. Gain",  "F.B.GAIN", -99.f, 99.0f, -50.0f, 1.0f, ParamFormat::SignedPercent,     true},
        {"Low EQ",     "LOW G",    -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
        {"High EQ",    "HIGH G",   -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
    }),
    // 10: ChorusA (#11 CHORUS A)
    makeMeta({
        {"Mod Freq",   "MOD FRQ",  0.1f,  20.0f, 0.2f,   0.1f, ParamFormat::FloatHertz,        true},
        {"DM Depth",   "DM DPTH",  0.0f,  100.0f,50.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Mod Delay",  "MOD DLY",  0.1f,  50.0f, 5.0f,   0.1f, ParamFormat::FloatMilliseconds, true},
        {"AM Depth",   "AM DPTH",  0.0f,  100.0f,40.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Low EQ",     "LOW G",    -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
        {"High EQ",    "HIGH G",   -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
    }),
    // 11: ChorusB (#12 CHORUS B)
    makeMeta({
        {"Mod Freq",   "MOD FRQ",  0.1f,  20.0f, 0.6f,   0.1f, ParamFormat::FloatHertz,        true},
        {"DM Depth",   "DM DPTH",  0.0f,  100.0f,50.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Mod Delay",  "MOD DLY",  0.1f,  50.0f, 3.5f,   0.1f, ParamFormat::FloatMilliseconds, true},
        {"AM Depth",   "AM DPTH",  0.0f,  100.0f,40.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Low EQ",     "LOW G",    -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
        {"High EQ",    "HIGH G",   -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
    }),
    // 12: StereoPhasing (#13 STEREO PHASING)
    makeMeta({
        {"Mod Freq",   "MOD FRQ",  0.1f,  20.0f, 0.5f,   0.1f, ParamFormat::FloatHertz,        true},
        {"Mod Depth",  "MOD DPTH", 0.0f,  100.0f,60.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Mod Delay",  "MOD DLY",  0.1f,  8.0f,  1.5f,   0.1f, ParamFormat::FloatMilliseconds, true},
        {"F.B. Gain",  "F.B.GAIN", 0.0f,  99.0f, 50.0f,  1.0f, ParamFormat::IntPercent,        true},
    }),
    // 13: Tremolo (#14 TREMOLO)
    makeMeta({
        {"Mod Freq",   "MOD FRQ",  0.1f,  20.0f, 5.0f,   0.1f, ParamFormat::FloatHertz,        true},
        {"AM Depth",   "AM DPTH",  0.0f,  100.0f,70.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"DM Depth",   "DM DPTH",  0.0f,  100.0f,10.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Low EQ",     "LOW G",    -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
        {"High EQ",    "HIGH G",   -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
    }),
    // 14: Symphonic (#15 SYMPHONIC - 3-voice short modulated delay chorus)
    makeMeta({
        {"Mod Freq",   "MOD FRQ",  0.1f,  20.0f, 0.7f,   0.1f, ParamFormat::FloatHertz,        true},
        {"Mod Depth",  "MOD DPTH", 0.0f,  100.0f,50.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Low EQ",     "LOW G",    -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
        {"High EQ",    "HIGH G",   -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
    }),
    // 15: GateReverb (#16 GATE REVERB)
    makeMeta({
        {"Type",       "TYPE",     0.0f,  3.0f,  1.0f,   1.0f, ParamFormat::ErType,            true},
        {"Room Size",  "ROOM SZ",  0.1f,  20.0f, 2.0f,   0.1f, ParamFormat::FloatRoomSize,     true},
        {"Liveness",   "LIVENESS", 0.0f,  10.0f, 5.0f,   1.0f, ParamFormat::IntLiveness,       true},
        {"Pre-Delay",  "DELAY",    0.1f,  400.0f,10.0f,  1.0f, ParamFormat::FloatMilliseconds, true},
        {"Hold Time",  "HOLD",     5.0f,  2000.f,120.0f, 5.0f, ParamFormat::FloatMilliseconds, true},
        {"Release",    "RELEASE",  5.0f,  2000.f,90.0f,  5.0f, ParamFormat::FloatMilliseconds, true},
        {"Trg Level",  "TRG LVL",  1.0f,  100.0f,35.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"LPF Cutoff", "LPF",      1000.f,12000.f,10000.f,500.f,ParamFormat::FilterLpf,        true},
    }),
    // 16: ReverseGate (#17 REVERSE GATE)
    makeMeta({
        {"Type",       "TYPE",     0.0f,  3.0f,  2.0f,   1.0f, ParamFormat::ErType,            true},
        {"Room Size",  "ROOM SZ",  0.1f,  20.0f, 2.0f,   0.1f, ParamFormat::FloatRoomSize,     true},
        {"Liveness",   "LIVENESS", 0.0f,  10.0f, 5.0f,   1.0f, ParamFormat::IntLiveness,       true},
        {"Pre-Delay",  "DELAY",    0.1f,  400.0f,10.0f,  1.0f, ParamFormat::FloatMilliseconds, true},
        {"Hold Time",  "HOLD",     5.0f,  2000.f,180.0f, 5.0f, ParamFormat::FloatMilliseconds, true},
        {"Release",    "RELEASE",  5.0f,  2000.f,60.0f,  5.0f, ParamFormat::FloatMilliseconds, true},
        {"Trg Level",  "TRG LVL",  1.0f,  100.0f,35.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"LPF Cutoff", "LPF",      1000.f,12000.f,10000.f,500.f,ParamFormat::FilterLpf,        true},
    }),
    // 17: AdrNoiseGate (#18 ADR / NOISE GATE)
    makeMeta({
        {"Trg Level",  "TRG LVL",  1.0f,  100.0f,40.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Trg Delay",  "TRG DLY",  -100.f,100.0f,0.0f,   1.0f, ParamFormat::FloatMilliseconds, true},
        {"Attack",     "ATTACK",   5.0f,  2000.f,5.0f,   5.0f, ParamFormat::FloatMilliseconds, true},
        {"Hold Time",  "HOLD",     5.0f,  2000.f,100.0f, 5.0f, ParamFormat::FloatMilliseconds, true},
        {"Release",    "RELEASE",  5.0f,  2000.f,250.0f, 5.0f, ParamFormat::FloatMilliseconds, true},
        {"MIDI Trg",   "MIDI TRG", 0.0f,  1.0f,  0.0f,   1.0f, ParamFormat::OnOff,             true},
    }),
    // 18: Compressor (#19 COMPRESSOR)
    makeMeta({
        {"Trg Level",  "SENS",     1.0f,  100.0f,50.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Trg Delay",  "TRG DLY",  -100.f,100.0f,-10.0f, 1.0f, ParamFormat::FloatMilliseconds, true},
        {"Attack",     "ATTACK",   5.0f,  200.0f,10.0f,  1.0f, ParamFormat::FloatMilliseconds, true},
        {"Release",    "RELEASE",  10.0f, 2000.f,150.0f, 5.0f, ParamFormat::FloatMilliseconds, true},
    }),
    // 19: ReverbAndGate (#20 REVERB & GATE)
    makeMeta({
        {"Rev Time",   "REV TIME", 0.3f,  99.0f, 2.0f,   0.1f, ParamFormat::FloatSeconds,      true},
        {"High Damp",  "HIGH",     0.1f,  1.0f,  0.6f,   0.1f, ParamFormat::FloatRatio,        true},
        {"Trg Level",  "TRG LVL",  1.0f,  100.0f,40.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Hold Time",  "HOLD",     5.0f,  2000.f,120.0f, 5.0f, ParamFormat::FloatMilliseconds, true},
        {"Release",    "RELEASE",  5.0f,  2000.f,90.0f,  5.0f, ParamFormat::FloatMilliseconds, true},
        {"HPF Cutoff", "HPF",      20.0f, 1000.f,20.0f,  10.f, ParamFormat::FilterHpf,         true},
        {"LPF Cutoff", "LPF",      1000.f,12000.f,8000.f,500.f,ParamFormat::FilterLpf,         true},
    }),
    // 20: PitchChangeA (#21 PITCH CHANGE A - 1 Voice + Feedback + MIDI)
    makeMeta({
        {"Pitch",      "PITCH",    -12.f, 12.0f, 0.0f,   1.0f, ParamFormat::IntSemitones,      true},
        {"Fine",       "FINE",     -100.f,100.0f,10.0f,  1.0f, ParamFormat::IntCents,          true},
        {"Delay",      "DELAY",    0.1f,  400.0f,0.1f,   1.0f, ParamFormat::FloatMilliseconds, true},
        {"F.B. Gain",  "F.B.GAIN", -99.f, 99.0f, 0.0f,   1.0f, ParamFormat::SignedPercent,     true},
        {"Base Key",   "BASE KEY", 0.0f,  96.0f, 0.0f,   1.0f, ParamFormat::BaseKey,           true},
    }),
    // 21: PitchChangeB (#22 PITCH CHANGE B - 2 Voice Mono)
    makeMeta({
        {"1 Pitch",    "1 PITCH",  -12.f, 12.0f, 0.0f,   1.0f, ParamFormat::IntSemitones,      true},
        {"1 Fine",     "1 FINE",   -100.f,100.0f,8.0f,   1.0f, ParamFormat::IntCents,          true},
        {"1 Delay",    "1 DLY",    0.1f,  400.0f,0.1f,   1.0f, ParamFormat::FloatMilliseconds, true},
        {"2 Pitch",    "2 PITCH",  -12.f, 12.0f, 0.0f,   1.0f, ParamFormat::IntSemitones,      true},
        {"2 Fine",     "2 FINE",   -100.f,100.0f,-8.0f,  1.0f, ParamFormat::IntCents,          true},
        {"2 Delay",    "2 DLY",    0.1f,  400.0f,20.0f,  1.0f, ParamFormat::FloatMilliseconds, true},
    }),
    // 22: PitchChangeC (#23 PITCH CHANGE C - 2 Voice Stereo Detune -8/+8)
    makeMeta({
        {"L Pitch",    "L PITCH",  -12.f, 12.0f, 0.0f,   1.0f, ParamFormat::IntSemitones,      true},
        {"L Fine",     "L FINE",   -100.f,100.0f,8.0f,   1.0f, ParamFormat::IntCents,          true},
        {"L Delay",    "L DLY",    0.1f,  400.0f,0.1f,   1.0f, ParamFormat::FloatMilliseconds, true},
        {"R Pitch",    "R PITCH",  -12.f, 12.0f, 0.0f,   1.0f, ParamFormat::IntSemitones,      true},
        {"R Fine",     "R FINE",   -100.f,100.0f,-8.0f,  1.0f, ParamFormat::IntCents,          true},
        {"R Delay",    "R DLY",    0.1f,  400.0f,0.1f,   1.0f, ParamFormat::FloatMilliseconds, true},
    }),
    // 23: PitchChangeD (#24 PITCH CHANGE D - Stereo + Feedback + MIDI)
    makeMeta({
        {"L Pitch",    "L PITCH",  -12.f, 12.0f, -5.0f,  1.0f, ParamFormat::IntSemitones,      true},
        {"L Fine",     "L FINE",   -100.f,100.0f,0.0f,   1.0f, ParamFormat::IntCents,          true},
        {"L F.B.",     "L F.B.",   -99.f, 99.0f, 0.0f,   1.0f, ParamFormat::SignedPercent,     true},
        {"R Pitch",    "R PITCH",  -12.f, 12.0f, 7.0f,   1.0f, ParamFormat::IntSemitones,      true},
        {"R Fine",     "R FINE",   -100.f,100.0f,0.0f,   1.0f, ParamFormat::IntCents,          true},
        {"R F.B.",     "R F.B.",   -99.f, 99.0f, 0.0f,   1.0f, ParamFormat::SignedPercent,     true},
        {"Base Key",   "BASE KEY", 0.0f,  96.0f, 60.0f,  1.0f, ParamFormat::BaseKey,           true},
    }),
    // 24: FreezeA (#25 FREEZE A - Sampler / Buffer Loop)
    makeMeta({
        {"Rec Mode",   "REC MODE", 0.0f,  1.0f,  1.0f,   1.0f, ParamFormat::FreezeMode,        true},
        {"Trg Level",  "TRG LVL",  1.0f,  100.0f,50.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Start Pt",   "START",    0.0f,  450.0f,0.0f,   5.0f, ParamFormat::FloatMilliseconds, true, true},
        {"End Pt",     "END",      10.0f, 500.0f,500.0f, 5.0f, ParamFormat::FloatMilliseconds, true, true},
        {"F.B. Gain",  "F.B.GAIN", 0.0f,  99.0f, 75.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Manual Trg", "TRIGGER",  0.0f,  1.0f,  0.0f,   1.0f, ParamFormat::OnOff,             true},
    }),
    // 25: FreezeB (#26 FREEZE B - Pitch Playable Sampler)
    makeMeta({
        {"Rec Mode",   "REC MODE", 0.0f,  1.0f,  1.0f,   1.0f, ParamFormat::FreezeMode,        true},
        {"Trg Level",  "TRG LVL",  1.0f,  100.0f,50.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Pitch",      "PITCH",    -12.f, 12.0f, 0.0f,   1.0f, ParamFormat::IntSemitones,      true},
        {"Fine",       "FINE",     -100.f,100.0f,0.0f,   1.0f, ParamFormat::IntCents,          true},
        {"Base Key",   "BASE KEY", 0.0f,  96.0f, 60.0f,  1.0f, ParamFormat::BaseKey,           true},
        {"Manual Trg", "TRIGGER",  0.0f,  1.0f,  0.0f,   1.0f, ParamFormat::OnOff,             true},
    }),
    // 26: AutoPan (#27 AUTO PAN)
    makeMeta({
        {"Pan Speed",  "SPEED",    0.1f,  20.0f, 0.7f,   0.1f, ParamFormat::FloatHertz,        true},
        {"Direction",  "DIR",      0.0f,  2.0f,  2.0f,   1.0f, ParamFormat::PanDirection,      true},
        {"Pan Depth",  "DEPTH",    0.0f,  100.0f,75.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Low EQ",     "LOW G",    -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
        {"High EQ",    "HIGH G",   -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
    }),
    // 27: TriggeredPan (#28 TRIGGERED PAN)
    makeMeta({
        {"Trg Level",  "TRG LVL",  1.0f,  100.0f,65.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Direction",  "DIR",      0.0f,  2.0f,  0.0f,   1.0f, ParamFormat::PanDirection,      true},
        {"Attack",     "ATTACK",   5.0f,  1000.f,20.0f,  5.0f, ParamFormat::FloatMilliseconds, true},
        {"Hold Time",  "HOLD",     5.0f,  1000.f,100.0f, 5.0f, ParamFormat::FloatMilliseconds, true},
        {"Release",    "RELEASE",  5.0f,  2000.f,200.0f, 5.0f, ParamFormat::FloatMilliseconds, true},
        {"Pan Depth",  "DEPTH",    0.0f,  100.0f,100.0f, 1.0f, ParamFormat::IntPercent,        true},
    }),
    // 28: DelayVibrato (#29 DELAY VIBRATO)
    makeMeta({
        {"Trg Level",  "TRG LVL",  1.0f,  100.0f,50.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Vib Delay",  "VIB DLY",  0.0f,  3000.f,400.0f, 10.f, ParamFormat::FloatMilliseconds, true},
        {"Max Speed",  "MAX SPD",  0.1f,  20.0f, 6.0f,   0.1f, ParamFormat::FloatHertz,        true},
        {"Max Depth",  "MAX DPTH", 0.0f,  100.0f,40.0f,  1.0f, ParamFormat::IntPercent,        true},
        {"Mod Delay",  "MOD DLY",  0.1f,  20.0f, 2.5f,   0.1f, ParamFormat::FloatMilliseconds, true},
    }),
    // 29: ParametricEQ (#30 PARAMETRIC EQ)
    makeMeta({
        {"HPF Cutoff", "HPF",      20.0f, 1000.f,20.0f,  10.f, ParamFormat::FilterHpf,         true},
        {"Low Gain",   "LOW G",    -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
        {"Mid Freq",   "MID FRQ",  315.f, 4000.f,500.0f, 25.f, ParamFormat::FloatHertz,        true},
        {"Mid Gain",   "MID G",    -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
        {"Mid Q",      "MID Q",    0.5f,  5.0f,  1.0f,   0.1f, ParamFormat::FloatQ,            true},
        {"High Freq",  "HI FRQ",   800.f, 11000.f,4000.f,100.f,ParamFormat::FloatHertz,        true},
        {"High Gain",  "HI G",     -15.f, 15.0f, 0.0f,   1.0f, ParamFormat::IntDecibels,       true},
        {"LPF Cutoff", "LPF",      1000.f,12000.f,12000.f,500.f,ParamFormat::FilterLpf,        true},
    })
}};

struct RomSpec {
    int number;
    const char* name;
    const char* category;
    AlgorithmType alg;
    float balance;
};

constexpr std::array<RomSpec, kNumRomPresets> kRomSpecs = {{
    { 1, "REV 1 HALL",      "REV",     AlgorithmType::RevHall,       50.0f},
    { 2, "REV 2 ROOM",      "REV",     AlgorithmType::RevRoom,       50.0f},
    { 3, "REV 3 VOCAL",     "REV",     AlgorithmType::RevVocal,      50.0f},
    { 4, "REV 4 PLATE",     "REV",     AlgorithmType::RevPlate,      50.0f},
    { 5, "EARLY REF 1",     "ER 1",    AlgorithmType::EarlyRef1,     50.0f},
    { 6, "EARLY REF 2",     "ER 2",    AlgorithmType::EarlyRef2,     50.0f},
    { 7, "DELAY L,R",       "DELAY",   AlgorithmType::DelayLR,       50.0f},
    { 8, "STEREO ECHO",     "ECHO",    AlgorithmType::StereoEcho,    50.0f},
    { 9, "STEREO FLANGE A", "MOD",     AlgorithmType::StereoFlangeA, 50.0f},
    {10, "STEREO FLANGE B", "MOD",     AlgorithmType::StereoFlangeB, 50.0f},
    {11, "CHORUS A",        "MOD",     AlgorithmType::ChorusA,       50.0f},
    {12, "CHORUS B",        "MOD",     AlgorithmType::ChorusB,       50.0f},
    {13, "STEREO PHASING",  "MOD",     AlgorithmType::StereoPhasing, 50.0f},
    {14, "TREMOLO",         "MOD",     AlgorithmType::Tremolo,       100.0f},
    {15, "SYMPHONIC",       "MOD",     AlgorithmType::Symphonic,     50.0f},
    {16, "GATE REVERB",     "GATE",    AlgorithmType::GateReverb,    65.0f},
    {17, "REVERSE GATE",    "GATE",    AlgorithmType::ReverseGate,   65.0f},
    {18, "ADR-NOISE GATE",  "GATE",    AlgorithmType::AdrNoiseGate,  100.0f},
    {19, "COMPRESSOR",      "GATE",    AlgorithmType::Compressor,    100.0f},
    {20, "REVERB & GATE",   "GATE",    AlgorithmType::ReverbAndGate, 60.0f},
    {21, "PITCH CHANGE A",  "PITCH",   AlgorithmType::PitchChangeA,  50.0f},
    {22, "PITCH CHANGE B",  "PITCH",   AlgorithmType::PitchChangeB,  50.0f},
    {23, "PITCH CHANGE C",  "PITCH",   AlgorithmType::PitchChangeC,  50.0f},
    {24, "PITCH CHANGE D",  "PITCH",   AlgorithmType::PitchChangeD,  50.0f},
    {25, "FREEZE A",        "FREEZE",  AlgorithmType::FreezeA,       100.0f},
    {26, "FREEZE B",        "FREEZE",  AlgorithmType::FreezeB,       100.0f},
    {27, "AUTO PAN",        "PAN",     AlgorithmType::AutoPan,       100.0f},
    {28, "TRIGGERED PAN",   "PAN",     AlgorithmType::TriggeredPan,  100.0f},
    {29, "DELAY VIBRATO",   "VIBRATO", AlgorithmType::DelayVibrato,  100.0f},
    {30, "PARAMETRIC EQ",   "PEQ",     AlgorithmType::ParametricEQ,  100.0f}
}};

const std::array<Spx90Program, kNumRomPresets>& getBuiltRomTable() {
    static const auto table = []() {
        std::array<Spx90Program, kNumRomPresets> arr{};
        for (size_t i = 0; i < kNumRomPresets; ++i) {
            const auto& spec = kRomSpecs[i];
            Spx90Program prog;
            prog.memoryNumber = spec.number;
            prog.name = spec.name;
            prog.category = spec.category;
            prog.algorithm = spec.alg;
            prog.isRom = true;
            prog.balance = spec.balance;
            prog.outLevel = 100.0f;
            const auto& metas = Spx90PresetDatabase::getAlgorithmParamMeta(spec.alg);
            for (size_t p = 0; p < kMaxProgramParams; ++p) {
                prog.params[p] = metas[p].defaultVal;
            }
            arr[i] = prog;
        }
        return arr;
    }();
    return table;
}

} // namespace

const std::array<ParamMeta, kMaxProgramParams>& Spx90PresetDatabase::getAlgorithmParamMeta(AlgorithmType alg) {
    auto idx = static_cast<size_t>(alg);
    if (idx >= kAlgMetas.size()) idx = 0;
    return kAlgMetas[idx];
}

const Spx90Program& Spx90PresetDatabase::getRomPreset(int romIndexZeroBased) {
    const auto& table = getBuiltRomTable();
    int clamped = std::clamp(romIndexZeroBased, 0, kNumRomPresets - 1);
    return table[static_cast<size_t>(clamped)];
}

std::array<Spx90Program, kTotalMemorySlots> Spx90PresetDatabase::createDefaultMemoryBank() {
    std::array<Spx90Program, kTotalMemorySlots> bank{};
    const auto& roms = getBuiltRomTable();
    for (int i = 0; i < kNumRomPresets; ++i) {
        bank[static_cast<size_t>(i)] = roms[static_cast<size_t>(i)];
    }
    // Initialize User RAM slots 31..90 with editable copies of the 30 ROM presets (2 cycles of 30)
    for (int i = 0; i < kNumUserPresets; ++i) {
        Spx90Program userProg = roms[static_cast<size_t>(i % kNumRomPresets)];
        userProg.memoryNumber = kNumRomPresets + 1 + i;
        userProg.isRom = false;
        bank[static_cast<size_t>(kNumRomPresets + i)] = userProg;
    }
    return bank;
}

std::string Spx90PresetDatabase::formatParamValue(float value, const ParamMeta& meta, RamModel ramModel) {
    char buf[32]{};
    const float ramScale = (ramModel == RamModel::Spx90_MkII && meta.scalesWithRam) ? 4.0f : 1.0f;

    switch (meta.format) {
        case ParamFormat::FloatSeconds:
            std::snprintf(buf, sizeof(buf), "%.1fs", value);
            break;
        case ParamFormat::FloatMilliseconds:
            std::snprintf(buf, sizeof(buf), "%.1fms", value * ramScale);
            break;
        case ParamFormat::FloatHertz:
            if (value >= 1000.0f)
                std::snprintf(buf, sizeof(buf), "%.2fkHz", value / 1000.0f);
            else if (value >= 100.0f)
                std::snprintf(buf, sizeof(buf), "%.0fHz", value);
            else
                std::snprintf(buf, sizeof(buf), "%.1fHz", value);
            break;
        case ParamFormat::FloatRatio:
            std::snprintf(buf, sizeof(buf), "%.1f", value);
            break;
        case ParamFormat::FloatRoomSize:
            std::snprintf(buf, sizeof(buf), "%.1f", value);
            break;
        case ParamFormat::IntPercent:
            std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(value)));
            break;
        case ParamFormat::SignedPercent: {
            int iv = static_cast<int>(std::round(value));
            std::snprintf(buf, sizeof(buf), "%+d%%", iv);
            break;
        }
        case ParamFormat::IntSemitones: {
            int iv = static_cast<int>(std::round(value));
            std::snprintf(buf, sizeof(buf), "%+d", iv);
            break;
        }
        case ParamFormat::IntCents: {
            int iv = static_cast<int>(std::round(value));
            std::snprintf(buf, sizeof(buf), "%+dc", iv);
            break;
        }
        case ParamFormat::IntDecibels: {
            int iv = static_cast<int>(std::round(value));
            std::snprintf(buf, sizeof(buf), "%+ddB", iv);
            break;
        }
        case ParamFormat::IntLiveness:
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::round(value)));
            break;
        case ParamFormat::FilterHpf:
            if (value <= 25.0f) return "THRU";
            std::snprintf(buf, sizeof(buf), "%.0fHz", value);
            break;
        case ParamFormat::FilterLpf:
            if (value >= 11500.0f) return "THRU";
            std::snprintf(buf, sizeof(buf), "%.1fkHz", value / 1000.0f);
            break;
        case ParamFormat::ErType: {
            int t = std::clamp(static_cast<int>(std::round(value)), 0, 3);
            static const char* kErNames[4] = {"HALL", "RANDOM", "REVERSE", "PLATE"};
            return kErNames[t];
        }
        case ParamFormat::PanDirection: {
            int d = std::clamp(static_cast<int>(std::round(value)), 0, 2);
            static const char* kDirNames[3] = {"L->R", "L<-R", "L<->R"};
            return kDirNames[d];
        }
        case ParamFormat::FreezeMode:
            return (std::round(value) < 0.5f) ? "MANUAL" : "AUTO";
        case ParamFormat::BaseKey: {
            int note = static_cast<int>(std::round(value));
            if (note < 36) return "OFF";
            static const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            int octave = (note / 12) - 2;
            std::snprintf(buf, sizeof(buf), "%s%d", kNoteNames[note % 12], octave);
            break;
        }
        case ParamFormat::OnOff:
            return (std::round(value) < 0.5f) ? "OFF" : "ON";
        case ParamFormat::FloatQ:
            std::snprintf(buf, sizeof(buf), "%.1f", value);
            break;
    }
    return std::string(buf);
}

std::string Spx90PresetDatabase::formatLcdParamLine(const ParamMeta& meta, float value, RamModel ramModel) {
    if (!meta.active) return "                ";
    std::string valStr = formatParamValue(value, meta, ramModel);
    std::string label = meta.shortLcdName;
    char line[32]{};
    std::snprintf(line, sizeof(line), "%-8.8s=%7.7s", label.c_str(), valStr.c_str());
    return std::string(line).substr(0, 16);
}

} // namespace aero
