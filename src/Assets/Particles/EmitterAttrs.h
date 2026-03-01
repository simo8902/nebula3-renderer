// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_EMITTERATTRS_H
#define NDEVC_EMITTERATTRS_H

#include <array>
#include <cassert>
#include "EnvelopeCurve.h"

namespace Particles {

class EmitterAttrs {
public:
    enum FloatAttr {
        EmissionDuration = 0,
        ActivityDistance,
        StartRotationMin,
        StartRotationMax,
        Gravity,
        ParticleStretch,
        VelocityRandomize,
        RotationRandomize,
        SizeRandomize,
        PrecalcTime,
        StartDelay,
        TextureTile,
        NumFloatAttrs,
    };

    enum BoolAttr {
        Looping = 0,
        RandomizeRotation,
        StretchToStart,
        RenderOldestFirst,
        ViewAngleFade,
        Billboard,
        NumBoolAttrs,
    };

    enum IntAttr {
        StretchDetail = 0,
        NumIntAttrs,
    };

    enum EnvelopeAttr {
        Red = 0,
        Green,
        Blue,
        Alpha,
        EmissionFrequency,
        LifeTime,
        StartVelocity,
        RotationVelocity,
        Size,
        SpreadMin,
        SpreadMax,
        AirResistance,
        VelocityFactor,
        Mass,
        TimeManipulator,
        Alignment0,
        NumEnvelopeAttrs,
    };

    EmitterAttrs() {
        floatValues.fill(0.0f);
        intValues.fill(0);
        boolValues.fill(false);
    }

    void SetFloat(FloatAttr key, float value) {
        floatValues[key] = value;
    }

    float GetFloat(FloatAttr key) const {
        return floatValues[key];
    }

    void SetBool(BoolAttr key, bool value) {
        boolValues[key] = value;
    }

    bool GetBool(BoolAttr key) const {
        return boolValues[key];
    }

    void SetInt(IntAttr key, int value) {
        intValues[key] = value;
    }

    int GetInt(IntAttr key) const {
        return intValues[key];
    }

    void SetEnvelope(EnvelopeAttr key, const EnvelopeCurve& value) {
        envelopeValues[key] = value;
    }

    const EnvelopeCurve& GetEnvelope(EnvelopeAttr key) const {
        return envelopeValues[key];
    }

    void LoadFromParsedData(const std::array<float, 9>& envData, EnvelopeAttr attr) {
        EnvelopeCurve curve;
        curve.Setup(envData[0], envData[1], envData[2], envData[3],
                    envData[4], envData[5], envData[6], envData[7],
                    static_cast<EnvelopeCurve::ModFunc>((int)envData[8]));
        SetEnvelope(attr, curve);
    }

private:
    std::array<float, NumFloatAttrs> floatValues;
    std::array<int, NumIntAttrs> intValues;
    std::array<bool, NumBoolAttrs> boolValues;
    std::array<EnvelopeCurve, NumEnvelopeAttrs> envelopeValues;
};

} // namespace Particles

#endif //NDEVC_EMITTERATTRS_H
