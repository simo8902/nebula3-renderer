# Actual Issues With AlphaTest & SimpleLayer

## Current Status: Both Work But Have Problems

### AlphaTest - What's Actually Wrong

**Current implementation (CORRECT approach):**
- Uses standard shader with `alphaTest` uniform
- `if (alphaTest > 0 && diffColor.a < alphaCutoff) discard;`
- Matches Nebula design (same shader, different state)

**Problems:**

1. **No Early-Z / Depth Pre-Pass**
   - AlphaTest objects write G-buffer and depth simultaneously
   - Causes overdraw when alphatest foliage overlaps solid geometry
   - Should render depth-only pass first, then G-buffer pass reads existing depth
   - **Impact**: Performance loss on dense foliage

2. **Shadow Maps Don't Respect Alpha**
   - Shadow pass renders alphatest objects as solid rectangles
   - Need to pass diffuse texture + alphaCutoff to shadow shader
   - Need `discard` in shadow fragment shader
   - **Impact**: Foliage casts solid rectangular shadows instead of cutout

3. **Hardcoded alphaCutoff = 0.5**
   - All alphatest uses 0.5 threshold
   - Different materials need different thresholds:
     - Grass: 0.706 (180/255)
     - Foliage: 0.502 (128/255)
     - General: 0.471 (120/255)
   - Should read `AlphaRef` from .n3 shader params
   - **Impact**: Some materials look wrong (too transparent or too solid)

4. **AlphaTest Objects Render After Solid**
   - Line 1947: alphatest renders AFTER solid batching
   - Should render in same batch, just with alphaTest=1 flag
   - **Impact**: State changes, potential depth issues

---

## Fixes Needed

### AlphaTest Fixes

#### 1. Depth Pre-Pass (CRITICAL)
Add before geometry pass:
```cpp
auto& depthPass = geometryGraph->addPass("DepthPrePass");
depthPass.writes = {"gDepth"};
depthPass.depthTest = true;
depthPass.depthWrite = true;
depthPass.clearDepth = false;
depthPass.colorWrite = false;
depthPass.execute = [this]() {
    // Render all alphatest objects depth-only
    // Use same shader but disable color writes
};
```

#### 2. Shadow Alpha Testing
In shadow pass, for alphatest objects:
```cpp
shadowShader->setInt("alphaTest", 1);
shadowShader->setFloat("alphaCutoff", dc.alphaCutoff);
shadowShader->setInt("DiffMap0", 0);
bindTexture(0, dc.tex[0]);
```

Shadow shader needs:
```glsl
uniform int alphaTest;
uniform float alphaCutoff;
uniform sampler2D DiffMap0;

void main() {
    if (alphaTest > 0) {
        float alpha = texture(DiffMap0, sUV).a;
        if (alpha < alphaCutoff) discard;
    }
    gl_FragDepth = gl_FragCoord.z;
}
```

#### 3. Per-Material AlphaRef
In model parsing (around line 1200):
```cpp
auto itAlphaRef = node->shader_params_int.find("AlphaRef");
if (itAlphaRef != node->shader_params_int.end()) {
    dc.alphaCutoff = float(itAlphaRef->second) / 255.0f;
} else {
    dc.alphaCutoff = dc.alphaTest ? 0.5f : 0.0f;
}
```

#### 4. Batch AlphaTest With Solid
Instead of separate alphatest batch, render in solid batch:
```cpp
// In solid batch loop
for (auto& dc : solidDraws) {
    shader->setInt(U_ALPHA_TEST, dc.alphaTest ? 1 : 0);
    shader->setFloat(U_ALPHA_CUTOFF, dc.alphaCutoff);
    // render...
}
```