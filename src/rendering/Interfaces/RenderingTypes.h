// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_RENDERINGTYPES_H
#define NDEVC_RENDERINGTYPES_H

#include <cstdint>
#include "glm.hpp"

namespace NDEVC::Graphics {

enum class Format {
    RGBA8_UNORM,
    RGB32F,
    RGBA32F,
    RGBA16F,
    RGB16F,
    D24_UNORM_S8_UINT,
    D32_FLOAT_S8_UINT,
    BC1,
    BC3,
    BC5
};

enum class BufferType {
    Vertex,
    Index,
    Uniform,
    Indirect,
    Storage
};

enum class TextureType {
    Texture2D,
    TextureCube
};

enum class SamplerFilter {
    Nearest,
    Linear,
    NearestMipmapNearest,
    LinearMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapLinear
};

enum class SamplerWrap {
    Repeat,
    ClampToEdge,
    MirroredRepeat,
    ClampToBorder
};

enum class CompareFunc {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstantColor,
    OneMinusConstantColor,
    ConstantAlpha,
    OneMinusConstantAlpha
};

enum class CullMode {
    None,
    Front,
    Back
};

struct DepthState {
    bool depthTest = true;
    bool depthWrite = true;
    CompareFunc depthFunc = CompareFunc::Greater;
};

struct BlendState {
    bool blendEnable = false;
    BlendFactor srcColor = BlendFactor::One;
    BlendFactor dstColor = BlendFactor::Zero;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::Zero;
};

struct RasterizerState {
    CullMode cullMode = CullMode::Back;
    bool frontCounterClockwise = false;
    bool scissorEnable = false;
};

enum class StencilOp {
    Keep,
    Zero,
    Replace,
    Incr,
    IncrWrap,
    Decr,
    DecrWrap,
    Invert
};

struct StencilState {
    bool stencilEnable = false;
    uint32_t readMask = 0xFF;
    uint32_t writeMask = 0xFF;
    CompareFunc stencilFunc = CompareFunc::Always;
    uint8_t ref = 0;
    StencilOp stencilFailOp = StencilOp::Keep;
    StencilOp depthFailOp = StencilOp::Keep;
    StencilOp depthPassOp = StencilOp::Keep;
};

struct Viewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct RenderStateDesc {
    DepthState depth;
    BlendState blend;
    RasterizerState rasterizer;
    StencilState stencil;
    glm::vec4 blendColor = glm::vec4(1.0f);
};

}
#endif