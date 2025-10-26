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

    // Per-instance attributes (buffer 2) - matches InstanceGPUData layout
    float3 instancePosition    [[attribute(4)]];
    float3 instanceRotation    [[attribute(5)]];
    float3 instanceScale       [[attribute(6)]];
    float4 instanceColor       [[attribute(7)]];
    float instanceTextureIndex [[attribute(8)]];
    float instanceLodAlpha     [[attribute(9)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 fragPos;
    float3 normal;
    float2 texCoord;
    float4 color;
    float4 fragPosLightSpace;
};

struct VertexOutInstanced {
    float4 position [[position]];
    float3 fragPos;
    float3 normal;
    float2 texCoord;
    float4 color;
    float textureIndex;
    float lodAlpha;
    float4 fragPosLightSpace;
};

struct Uniforms {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4x4 normalMatrix;
    float4x4 lightViewProj;
    float4 materialColor;
    float3 lightPos;
    float alphaCutoff;
    float3 viewPos;
    float baseAlpha;
    float3 lightColor;
    float shadowBias;
    float uTime;
    float ditherScale;
    float crossfadeDuration;
    float padMisc;
    float4 lightingParams;
    float4 materialParams;
    int useTexture;
    int useTextureArray;
    int uDitherEnabled;
    int shadowsEnabled;
};

float sampleShadow(depth2d<float> shadowMap,
                   sampler shadowSampler,
                   float4 fragPosLightSpace,
                   float shadowBias) {
    float3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    float2 shadowUV = projCoords.xy * 0.5 + 0.5;
    float depth = projCoords.z;
    if (depth > 1.0) {
        return 1.0;
    }

    float width = float(shadowMap.get_width());
    float height = float(shadowMap.get_height());
    if (width <= 0.0 || height <= 0.0) {
        return 1.0;
    }

    float2 texelSize = 1.0 / float2(width, height);

    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0) {
        return 1.0;
    }

    float visibility = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float2 offset = float2(x, y) * texelSize;
            float2 sampleCoord = shadowUV + offset;
            float comparisonDepth = depth - shadowBias;
            visibility += shadowMap.sample_compare(shadowSampler,
                                                   sampleCoord,
                                                   comparisonDepth);
        }
    }
    return clamp(visibility / 9.0, 0.0f, 1.0f);
}

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
    out.fragPosLightSpace = uniforms.lightViewProj * worldPos;
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
    depth2d<float> shadowMap [[texture(2)]],
    sampler textureSampler [[sampler(0)]],
    sampler shadowSampler [[sampler(2)]]
) {
    float3 norm = normalize(in.normal);
    float3 lightDir = normalize(uniforms.lightPos - in.fragPos);
    float3 viewDir = normalize(uniforms.viewPos - in.fragPos);
    float3 reflectDir = reflect(-lightDir, norm);

    float diff = max(dot(norm, lightDir), 0.0);

    float lightIntensity = uniforms.lightingParams.x;
    float ambientStrength = uniforms.lightingParams.y;
    float roughness = clamp(uniforms.materialParams.x, 0.02f, 1.0f);
    float metallic = clamp(uniforms.materialParams.y, 0.0f, 1.0f);
    float glareStrength = max(uniforms.materialParams.z, 0.0f);

    float3 diffuse = diff * uniforms.lightColor * lightIntensity;

    float shininess = mix(8.0f, 128.0f, 1.0f - roughness);
    float specAngle = max(dot(viewDir, reflectDir), 0.0f);
    float spec = pow(specAngle, shininess);
    float specularStrength = mix(0.1f, 1.0f, metallic);
    float3 specular = specularStrength * spec * uniforms.lightColor * lightIntensity;

    float3 ambient = ambientStrength * uniforms.lightColor * lightIntensity;

    float shadowFactor = 1.0;
    if (uniforms.shadowsEnabled != 0) {
        shadowFactor = sampleShadow(shadowMap, shadowSampler,
                                    in.fragPosLightSpace,
                                    uniforms.shadowBias);
    }

    float4 baseColor = uniforms.materialColor * in.color;
    if (uniforms.useTexture != 0) {
        baseColor *= colorTexture.sample(textureSampler, in.texCoord);
    }

    float3 lighting = ambient + (diffuse + specular) * shadowFactor;
    float3 result = lighting * baseColor.rgb;

    float3 glare = glareStrength * spec * uniforms.lightColor;
    result += glare;

    float alpha = baseColor.a;
    return float4(result, alpha);
}

// ============================================================================
// Instanced Rendering Shaders
// ============================================================================

vertex VertexOutInstanced vertex_instanced(
    VertexInInstanced in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]]
) {
    VertexOutInstanced out;

    // Apply scale, rotation (XYZ Euler), and translation using per-instance data
    float3 scaledPos = in.position * in.instanceScale;

    float sx = sin(in.instanceRotation.x);
    float cx = cos(in.instanceRotation.x);
    float sy = sin(in.instanceRotation.y);
    float cy = cos(in.instanceRotation.y);
    float sz = sin(in.instanceRotation.z);
    float cz = cos(in.instanceRotation.z);

    float3x3 rotX = float3x3(
        float3(1.0, 0.0, 0.0),
        float3(0.0,  cx, -sx),
        float3(0.0,  sx,  cx)
    );

    float3x3 rotY = float3x3(
        float3( cy, 0.0, sy),
        float3(0.0, 1.0, 0.0),
        float3(-sy, 0.0, cy)
    );

    float3x3 rotZ = float3x3(
        float3( cz, -sz, 0.0),
        float3( sz,  cz, 0.0),
        float3(0.0, 0.0, 1.0)
    );

    float3x3 rotation = rotZ * rotY * rotX;

    float3 rotatedPos = rotation * scaledPos;
    float3 worldPosition = rotatedPos + in.instancePosition;

    float4 worldPos = uniforms.model * float4(worldPosition, 1.0);
    out.fragPos = worldPos.xyz;
    out.position = uniforms.projection * uniforms.view * worldPos;

    // Transform normal using instance rotation and the uniform normal matrix
    float3 transformedNormal = rotation * in.normal;
    out.normal = (uniforms.normalMatrix * float4(transformedNormal, 0.0)).xyz;

    // Pass through texture coordinates
    out.texCoord = in.texCoord;

    // Mix vertex color with instance color
    out.color = in.color * in.instanceColor;

    // Pass instance-specific data
    out.textureIndex = in.instanceTextureIndex;
    out.lodAlpha = in.instanceLodAlpha;
    out.fragPosLightSpace = uniforms.lightViewProj * worldPos;

    return out;
}

fragment float4 fragment_instanced(
    VertexOutInstanced in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]],
    texture2d<float> colorTexture [[texture(0)]],
    texture2d_array<float> textureArray [[texture(1)]],
    depth2d<float> shadowMap [[texture(2)]],
    sampler textureSampler [[sampler(0)]],
    sampler shadowSampler [[sampler(2)]]
) {
    float3 norm = normalize(in.normal);
    float3 lightDir = normalize(uniforms.lightPos - in.fragPos);
    float3 viewDir = normalize(uniforms.viewPos - in.fragPos);
    float3 reflectDir = reflect(-lightDir, norm);

    float diff = max(dot(norm, lightDir), 0.0);

    float lightIntensity = uniforms.lightingParams.x;
    float ambientStrength = uniforms.lightingParams.y;
    float roughness = clamp(uniforms.materialParams.x, 0.02f, 1.0f);
    float metallic = clamp(uniforms.materialParams.y, 0.0f, 1.0f);
    float glareStrength = max(uniforms.materialParams.z, 0.0f);

    float3 diffuse = diff * uniforms.lightColor * lightIntensity;

    float shininess = mix(8.0f, 128.0f, 1.0f - roughness);
    float specAngle = max(dot(viewDir, reflectDir), 0.0f);
    float spec = pow(specAngle, shininess);
    float specularStrength = mix(0.1f, 1.0f, metallic);
    float3 specular = specularStrength * spec * uniforms.lightColor * lightIntensity;

    float3 ambient = ambientStrength * uniforms.lightColor * lightIntensity;

    float shadowFactor = 1.0;
    if (uniforms.shadowsEnabled != 0) {
        shadowFactor = sampleShadow(shadowMap, shadowSampler,
                                    in.fragPosLightSpace,
                                    uniforms.shadowBias);
    }

    float4 sampledColor = float4(1.0);
    if (uniforms.useTextureArray != 0) {
        uint layerCount = textureArray.get_array_size();
        uint texIndex = layerCount > 0
            ? uint(clamp(in.textureIndex, 0.0f, float(layerCount - 1)))
            : 0u;
        sampledColor = textureArray.sample(textureSampler, in.texCoord, texIndex);
    } else if (uniforms.useTexture != 0) {
        sampledColor = colorTexture.sample(textureSampler, in.texCoord);
    }

    float4 baseColor = sampledColor * uniforms.materialColor * in.color;

    float3 lighting = ambient + (diffuse + specular) * shadowFactor;
    float3 result = lighting * baseColor.rgb;

    float3 glare = glareStrength * spec * uniforms.lightColor;
    result += glare;

    float alpha = baseColor.a * in.lodAlpha;
    return float4(result, alpha);
}

// ============================================================================
// Shadow Depth Only Pass
// ============================================================================

struct ShadowVertexOut {
    float4 position [[position]];
    float2 texCoord;
    float vertexAlpha;
    float textureIndex;
};

vertex ShadowVertexOut vertex_shadow_depth(
    VertexIn in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]]) {
    ShadowVertexOut out;
    float4 worldPos = uniforms.model * float4(in.position, 1.0);
    out.position = uniforms.lightViewProj * worldPos;
    out.texCoord = in.texCoord;
    out.vertexAlpha = in.color.a;
    out.textureIndex = 0.0f;
    return out;
}

vertex ShadowVertexOut vertex_shadow_depth_instanced(
    VertexInInstanced in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]]) {
    ShadowVertexOut out;

    float3 scaledPos = in.position * in.instanceScale;

    float sx = sin(in.instanceRotation.x);
    float cx = cos(in.instanceRotation.x);
    float sy = sin(in.instanceRotation.y);
    float cy = cos(in.instanceRotation.y);
    float sz = sin(in.instanceRotation.z);
    float cz = cos(in.instanceRotation.z);

    float3x3 rotX = float3x3(
        float3(1.0, 0.0, 0.0),
        float3(0.0,  cx, -sx),
        float3(0.0,  sx,  cx)
    );

    float3x3 rotY = float3x3(
        float3( cy, 0.0, sy),
        float3(0.0, 1.0, 0.0),
        float3(-sy, 0.0, cy)
    );

    float3x3 rotZ = float3x3(
        float3( cz, -sz, 0.0),
        float3( sz,  cz, 0.0),
        float3(0.0, 0.0, 1.0)
    );

    float3x3 rotation = rotZ * rotY * rotX;
    float3 rotatedPos = rotation * scaledPos;
    float3 worldPosition = rotatedPos + in.instancePosition;

    float4 worldPos = uniforms.model * float4(worldPosition, 1.0);
    out.position = uniforms.lightViewProj * worldPos;
    out.texCoord = in.texCoord;
    out.vertexAlpha = in.color.a * in.instanceColor.a;
    out.textureIndex = in.instanceTextureIndex;
    return out;
}

fragment void fragment_shadow_depth(
    ShadowVertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]],
    texture2d<float> colorTexture [[texture(0)]],
    texture2d_array<float> textureArray [[texture(1)]],
    sampler textureSampler [[sampler(0)]]) {
    float alpha = uniforms.baseAlpha * in.vertexAlpha;

    if (uniforms.useTextureArray != 0) {
        uint layerCount = textureArray.get_array_size();
        if (layerCount > 0) {
            float roundedIndex = floor(in.textureIndex + 0.5f);
            uint clampedIndex = min(uint(max(0.0f, roundedIndex)), layerCount - 1);
            alpha *= textureArray.sample(textureSampler, in.texCoord, clampedIndex).a;
        }
    } else if (uniforms.useTexture != 0) {
        alpha *= colorTexture.sample(textureSampler, in.texCoord).a;
    }

    if (alpha < uniforms.alphaCutoff) {
        discard_fragment();
    }
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
