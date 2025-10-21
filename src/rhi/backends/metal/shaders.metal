// src/rhi/backends/metal/shaders.metal
#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

// ============================================================================
// Common Structures
// ============================================================================

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
    float4 color    [[attribute(3)]];
};

// Combined struct for instanced rendering - all stage_in data in ONE struct
struct VertexInInstanced {
    // Per-vertex attributes (buffer 0)
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
    float4 color    [[attribute(3)]];

    // Per-instance attributes (buffer 2)
    // Pre-calculated transformation matrix (4x4 matrix uses 4 attribute slots)
    float4 instanceTransformCol0 [[attribute(4)]];
    float4 instanceTransformCol1 [[attribute(5)]];
    float4 instanceTransformCol2 [[attribute(6)]];
    float4 instanceTransformCol3 [[attribute(7)]];
    float4 instanceColor         [[attribute(8)]];
    float instanceTextureIndex   [[attribute(9)]];
    float instanceLodAlpha       [[attribute(10)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 fragPos;
    float3 normal;
    float2 texCoord;
    float4 color;
};

struct VertexOutInstanced {
    float4 position [[position]];
    float3 fragPos;
    float3 normal;
    float2 texCoord;
    float4 color;
    float textureIndex;
    float lodAlpha;
};

struct Uniforms {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4x4 normalMatrix;  // ADDED: Inverse-transpose of model matrix for correct normal transformation
    float3 lightPos;
    float3 viewPos;
    float time;
    int useTexture;
    int useTextureArray;
    int ditherEnabled;
};

// ============================================================================
// Standard Vertex Shader
// ============================================================================

vertex VertexOut vertex_main(
    VertexIn in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]]
) {
    VertexOut out;
    
    float4 worldPos = uniforms.model * float4(in.position, 1.0);
    out.fragPos = worldPos.xyz;
    
    // FIXED: Use normalMatrix (inverse-transpose of model) for correct normal transformation
    // This handles non-uniform scaling correctly
    out.normal = (uniforms.normalMatrix * float4(in.normal, 0.0)).xyz;
    
    out.texCoord = in.texCoord;
    out.color = in.color;
    out.position = uniforms.projection * uniforms.view * worldPos;
    
    return out;
}

// ============================================================================
// Standard Fragment Shader
// ============================================================================

fragment float4 fragment_main(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]],
    texture2d<float> colorTexture [[texture(0)]],
    sampler textureSampler [[sampler(0)]]
) {
    float3 norm = normalize(in.normal);
    float3 lightDir = normalize(uniforms.lightPos - in.fragPos);
    float3 viewDir = normalize(uniforms.viewPos - in.fragPos);
    float3 reflectDir = reflect(-lightDir, norm);

    // Lighting calculations
    float diff = max(dot(norm, lightDir), 0.0);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);

    float3 ambient = 0.3 * in.color.rgb;
    float3 diffuse = diff * in.color.rgb;
    float3 specular = spec * 0.5 * float3(1.0);

    float4 finalColor;
    if (uniforms.useTexture != 0) {
        float4 texColor = colorTexture.sample(textureSampler, in.texCoord);
        float3 result = (ambient + diffuse + specular) * texColor.rgb * in.color.rgb;
        finalColor = float4(result, texColor.a * in.color.a);
    } else {
        float3 result = ambient + diffuse + specular;
        finalColor = float4(result, in.color.a);
    }

    return finalColor;
}

// ============================================================================
// Instanced Rendering Shaders
// ============================================================================

// OPTIMIZED: Uses pre-calculated transformation matrix from CPU
vertex VertexOutInstanced vertex_instanced(
    VertexInInstanced in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]]
) {
    VertexOutInstanced out;

    // Reconstruct the pre-calculated transformation matrix from the 4 column vectors
    float4x4 instanceTransform = float4x4(
        in.instanceTransformCol0,
        in.instanceTransformCol1,
        in.instanceTransformCol2,
        in.instanceTransformCol3
    );

    // Apply instance transform to vertex position (calculated once on CPU)
    float4 localPos = float4(in.position, 1.0);
    float4 instancedPos = instanceTransform * localPos;

    // Apply model, view, projection transforms
    float4 worldPos = uniforms.model * instancedPos;
    out.fragPos = worldPos.xyz;
    out.position = uniforms.projection * uniforms.view * worldPos;

    // FIXED: Use normalMatrix for correct normal transformation
    // Transform normal using normalMatrix combined with instance transform
    // Note: For instances, we also need to account for the instance transform's rotation/scale
    // Extract 3x3 from instanceTransform for normal (assuming uniform scaling per instance for now)
    float3x3 instanceRotScale = float3x3(
        instanceTransform[0].xyz,
        instanceTransform[1].xyz,
        instanceTransform[2].xyz
    );
    float3 transformedNormal = instanceRotScale * in.normal;
    out.normal = (uniforms.normalMatrix * float4(transformedNormal, 0.0)).xyz;

    // Pass through texture coordinates
    out.texCoord = in.texCoord;

    // Mix vertex color with instance color
    out.color = in.color * in.instanceColor;

    // Pass instance-specific data
    out.textureIndex = in.instanceTextureIndex;
    out.lodAlpha = in.instanceLodAlpha;

    return out;
}

fragment float4 fragment_instanced(
    VertexOutInstanced in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]],
    texture2d<float> colorTexture [[texture(0)]],
    texture2d_array<float> textureArray [[texture(1)]],
    sampler textureSampler [[sampler(0)]]
) {
    float3 norm = normalize(in.normal);
    float3 lightDir = normalize(uniforms.lightPos - in.fragPos);
    float3 viewDir = normalize(uniforms.viewPos - in.fragPos);
    float3 reflectDir = reflect(-lightDir, norm);

    // Lighting calculations
    float diff = max(dot(norm, lightDir), 0.0);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);

    float3 ambient = 0.3 * in.color.rgb;
    float3 diffuse = diff * in.color.rgb;
    float3 specular = spec * 0.5 * float3(1.0);

    float4 finalColor;
    if (uniforms.useTextureArray != 0) {
        // Sample from texture array using instance texture index
        uint texIndex = uint(in.textureIndex);
        float4 texColor = textureArray.sample(textureSampler, in.texCoord, texIndex);
        float3 result = (ambient + diffuse + specular) * texColor.rgb * in.color.rgb;
        finalColor = float4(result, texColor.a * in.color.a);
    } else if (uniforms.useTexture != 0) {
        float4 texColor = colorTexture.sample(textureSampler, in.texCoord);
        float3 result = (ambient + diffuse + specular) * texColor.rgb * in.color.rgb;
        finalColor = float4(result, texColor.a * in.color.a);
    } else {
        float3 result = ambient + diffuse + specular;
        finalColor = float4(result, in.color.a);
    }

    // Apply LOD alpha for crossfading
    finalColor.a *= in.lodAlpha;

    return finalColor;
}

// ============================================================================
// Compute Shaders
// ============================================================================

struct CullingData {
    float4 position;
    float radius;
};

kernel void culling_compute(
    device CullingData* instances [[buffer(0)]],
    constant float4x4& viewProj [[buffer(1)]],
    device atomic_uint* visibleCount [[buffer(2)]],
    uint gid [[thread_position_in_grid]]
) {
    // Frustum culling logic here
    CullingData data = instances[gid];
    
    // Simple sphere frustum culling (simplified)
    float4 viewPos = viewProj * data.position;
    
    // Check if in frustum (simplified - just check against near/far planes)
    if (viewPos.z > -data.radius && viewPos.z < 1000.0) {
        atomic_fetch_add_explicit(visibleCount, 1, memory_order_relaxed);
    }
}

struct LODData {
    float distance;
    uint lodLevel;
};

kernel void lod_compute(
    device LODData* instances [[buffer(0)]],
    constant float3& cameraPos [[buffer(1)]],
    uint gid [[thread_position_in_grid]]
) {
    // LOD selection logic here
    LODData data = instances[gid];

    // Calculate distance-based LOD (simplified)
    if (data.distance < 10.0) {
        data.lodLevel = 0;
    } else if (data.distance < 50.0) {
        data.lodLevel = 1;
    } else {
        data.lodLevel = 2;
    }

    instances[gid] = data;
}

kernel void test_compute(
    device const uint* inputData [[buffer(0)]],
    device uint* outputData [[buffer(1)]],
    device uint* accumData [[buffer(2)]],
    constant uint& elementCount [[buffer(3)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid >= elementCount) {
        return;
    }

    uint value = inputData[gid];
    accumData[gid] += value;
    outputData[gid] = value * 2u;
}
