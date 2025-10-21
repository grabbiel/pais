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
    float3 instancePosition [[attribute(4)]];
    float3 instanceRotation [[attribute(5)]];
    float3 instanceScale    [[attribute(6)]];
    float4 instanceColor    [[attribute(7)]];
    float instanceTextureIndex [[attribute(8)]];
    float instanceLodAlpha     [[attribute(9)]];
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
    out.normal = (uniforms.model * float4(in.normal, 0.0)).xyz;
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

// Helper function: Create rotation matrix from Euler angles (XYZ order)
float4x4 createRotationMatrix(float3 rotation) {
    float cx = cos(rotation.x);
    float sx = sin(rotation.x);
    float cy = cos(rotation.y);
    float sy = sin(rotation.y);
    float cz = cos(rotation.z);
    float sz = sin(rotation.z);

    float4x4 rotX = float4x4(
        float4(1.0, 0.0, 0.0, 0.0),
        float4(0.0, cx, -sx, 0.0),
        float4(0.0, sx, cx, 0.0),
        float4(0.0, 0.0, 0.0, 1.0)
    );

    float4x4 rotY = float4x4(
        float4(cy, 0.0, sy, 0.0),
        float4(0.0, 1.0, 0.0, 0.0),
        float4(-sy, 0.0, cy, 0.0),
        float4(0.0, 0.0, 0.0, 1.0)
    );

    float4x4 rotZ = float4x4(
        float4(cz, -sz, 0.0, 0.0),
        float4(sz, cz, 0.0, 0.0),
        float4(0.0, 0.0, 1.0, 0.0),
        float4(0.0, 0.0, 0.0, 1.0)
    );

    return rotZ * rotY * rotX;
}

// FIXED: Single stage_in parameter with all vertex and instance data combined
vertex VertexOutInstanced vertex_instanced(
    VertexInInstanced in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]]
) {
    VertexOutInstanced out;

    // Create instance transform matrix using instance attributes
    float4x4 scaleMatrix = float4x4(
        float4(in.instanceScale.x, 0.0, 0.0, 0.0),
        float4(0.0, in.instanceScale.y, 0.0, 0.0),
        float4(0.0, 0.0, in.instanceScale.z, 0.0),
        float4(0.0, 0.0, 0.0, 1.0)
    );

    float4x4 rotationMatrix = createRotationMatrix(in.instanceRotation);

    float4x4 translationMatrix = float4x4(
        float4(1.0, 0.0, 0.0, 0.0),
        float4(0.0, 1.0, 0.0, 0.0),
        float4(0.0, 0.0, 1.0, 0.0),
        float4(in.instancePosition.x, in.instancePosition.y, in.instancePosition.z, 1.0)
    );

    // Combine transformations: Translation * Rotation * Scale
    float4x4 instanceTransform = translationMatrix * rotationMatrix * scaleMatrix;

    // Apply instance transform to vertex position
    float4 localPos = float4(in.position, 1.0);
    float4 instancedPos = instanceTransform * localPos;

    // Apply model, view, projection transforms
    float4 worldPos = uniforms.model * instancedPos;
    out.fragPos = worldPos.xyz;
    out.position = uniforms.projection * uniforms.view * worldPos;

    // Transform normal (only rotation and scale, no translation)
    float4x4 normalTransform = rotationMatrix * scaleMatrix;
    out.normal = (uniforms.model * normalTransform * float4(in.normal, 0.0)).xyz;

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
