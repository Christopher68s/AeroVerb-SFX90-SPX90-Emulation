#pragma once

#include "Spx90Types.h"
#include <array>
#include <string>

namespace aero {

class Spx90PresetDatabase {
public:
    static const std::array<ParamMeta, kMaxProgramParams>& getAlgorithmParamMeta(AlgorithmType alg);
    static const Spx90Program& getRomPreset(int romIndexZeroBased);
    static std::array<Spx90Program, kTotalMemorySlots> createDefaultMemoryBank();

    // Format parameter value for the 16x2 dot-matrix LCD and UI readouts
    static std::string formatParamValue(float value, const ParamMeta& meta, RamModel ramModel = RamModel::Spx90_MkI);

    // Format a full 16-char LCD line for parameter editing
    static std::string formatLcdParamLine(const ParamMeta& meta, float value, RamModel ramModel = RamModel::Spx90_MkI);
};

} // namespace aero
