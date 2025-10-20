// src/renderer3d/metal/shaders.metal
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

struct VertexOut {
    float4 position [[position]];
    float3 fragPos;
    float3 normal;
    float2 texCoord;
    float4 color;
};

struct InstanceData {
    float3 position;
    float3 rotation;
    float3 scale;
    float4 color;
    float textureIndex;
    float cullingRadius;
    float lodTransitionAlpha;
    float _padding;
};

struct InstanceVertexOut {
    float4 position [[position]];
    float3 fragPos;
    float3 normal;
    float2 texCoord;
    float4 color;
    float textureIndex;
    float lodTransitionAlpha;
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

struct CullingUniforms {
    float4x4 viewProjection;
    float4 frustumPlanes[6];
    float3 cameraPosition;
    uint totalInstances;
    uint baseIndexCount;
};

struct LODUniforms {
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float3 cameraPosition;
    uint totalInstances;
    int viewportHeight;
    int lodMode; // 0=distance, 1=screenspace, 2=hybrid
    float distanceHigh;
    float distanceMedium;
    float distanceCull;
    float screenspaceHigh;
    float screenspaceMedium;
    float screenspaceLow;
    float hybridWeight;
    float4 frustumPlanes[6];
};

// ============================================================================
// Helper Functions
// ============================================================================

float4x4 rotationMatrix(float3 axis, float angle) {
    axis = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;
    
    return float4x4(
        oc * axis.x * axis.x + c,           oc * axis.x * axis.y - axis.z * s,  oc * axis.z * axis.x + axis.y * s,  0.0,
        oc * axis.x * axis.y + axis.z * s,  oc * axis.y * axis.y + c,           oc * axis.y * axis.z - axis.x * s,  0.0,
        oc * axis.z * axis.x - axis.y * s,  oc * axis.y * axis.z + axis.x * s,  oc * axis.z * axis.z + c,           0.0,
        0.0,                                 0.0,                                 0.0,                                 1.0
    );
}

float getBayerValue(float2 pos) {
    int x = int(fmod(pos.x, 4.0));
    int y = int(fmod(pos.y, 4.0));
    
    const float bayer[16] = {
        0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
        12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
        3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
        15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
    };
    
    return bayer[y * 4 + x];
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
    out.normal = (uniforms.model * float4(in.normal, 0.0)).xyz;
    out.texCoord = in.texCoord;
    out.color = in.color;
    out.position = uniforms.projection * uniforms.view * worldPos;
    
    return out;
}

// ============================================================================
// Instanced Vertex Shader
// ============================================================================

vertex InstanceVertexOut vertex_instanced(
    VertexIn in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]],
    const device InstanceData* instances [[buffer(2)]],
    uint instanceID [[instance_id]]
) {
    InstanceVertexOut out;
    
    InstanceData inst = instances[instanceID];
    
    // Apply rotation
    float4x4 rotX = rotationMatrix(float3(1,0,0), inst.rotation.x);
    float4x4 rotY = rotationMatrix(float3(0,1,0), inst.rotation.y);
    float4x4 rotZ = rotationMatrix(float3(0,0,1), inst.rotation.z);
    float4x4 rotation = rotZ * rotY * rotX;
    
    // Apply scale and rotation
    float4 scaledPos = float4(in.position * inst.scale, 1.0);
    float4 rotatedPos = rotation * scaledPos;
    float4 worldPos = rotatedPos + float4(inst.position, 0.0);
    
    out.fragPos = worldPos.xyz;
    out.normal = (rotation * float4(in.normal, 0.0)).xyz;
    out.texCoord = in.texCoord;
    out.color = in.color * inst.color;
    out.textureIndex = inst.textureIndex;
    out.lodTransitionAlpha = inst.lodTransitionAlpha;
    out.position = uniforms.projection * uniforms.view * worldPos;
    
    return out;
}

// ============================================================================
// Standard Fragment Shader
// ============================================================================

fragment float4 fragment_main(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]],
    texture2d<float> texture [[texture(0)]],
    sampler textureSampler [[sampler(0)]]
) {
    float3 norm = normalize(in.normal);
    float3 lightDir = normalize(uniforms.lightPos - in.fragPos);
    float3 viewDir = normalize(uniforms.viewPos - in.fragPos);
    float3 reflectDir = reflect(-lightDir, norm);
    
    float diff = max(dot(norm, lightDir), 0.0);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    
    float3 ambient = 0.3 * in.color.rgb;
    float3 diffuse = diff * in.color.rgb;
    float3 specular = spec * 0.5 * float3(1.0);
    
    float4 finalColor;
    if (uniforms.useTexture) {
        float4 texColor = texture.sample(textureSampler, in.texCoord);
        float3 result = (ambient + diffuse + specular) * texColor.rgb * in.color.rgb;
        finalColor = float4(result, texColor.a * in.color.a);
    } else {
        float3 result = ambient + diffuse + specular;
        finalColor = float4(result, in.color.a);
    }
    
    return finalColor;
}

// ============================================================================
// Instanced Fragment Shader with Dithered LOD
// ============================================================================

fragment float4 fragment_instanced(
    InstanceVertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]],
    texture2d_array<float> textureArray [[texture(0)]],
    sampler textureSampler [[sampler(0)]],
    float2 pointCoord [[point_coord]]
) {
    // Dithered LOD transition
    if (uniforms.ditherEnabled > 0 && in.lodTransitionAlpha < 1.0) {
        float threshold = getBayerValue(in.position.xy);
        
        // Optional temporal jitter
        if (uniforms.ditherEnabled > 1) {
            float jitter = fract(uniforms.time * 0.5);
            threshold = fract(threshold + jitter);
        }
        
        if (in.lodTransitionAlpha < threshold) {
            discard_fragment();
        }
    }
    
    // Standard lighting
    float3 norm = normalize(in.normal);
    float3 lightDir = normalize(uniforms.lightPos - in.fragPos);
    float3 viewDir = normalize(uniforms.viewPos - in.fragPos);
    float3 reflectDir = reflect(-lightDir, norm);
    
    float diff = max(dot(norm, lightDir), 0.0);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    
    float3 ambient = 0.3 * in.color.rgb;
    float3 diffuse = diff * in.color.rgb;
    float3 specular = spec * 0.5 * float3(1.0);
    
    float4 finalColor;
    if (uniforms.useTextureArray == 1) {
        float4 texColor = textureArray.sample(textureSampler, in.texCoord, uint(in.textureIndex));
        float3 result = (ambient + diffuse + specular) * texColor.rgb * in.color.rgb;
        finalColor = float4(result, texColor.a * in.color.a);
    } else {
        float3 result = ambient + diffuse + specular;
        finalColor = float4(result, in.color.a);
    }
    
    return finalColor;
}

// ============================================================================
// GPU Culling Compute Shader
// ============================================================================

bool isVisible(float3 center, float radius, constant float4* frustumPlanes) {
    for (int i = 0; i < 6; i++) {
        float4 plane = frustumPlanes[i];
        float distance = dot(plane.xyz, center) + plane.w;
        if (distance < -radius) {
            return false;
        }
    }
    return true;
}

kernel void culling_compute(
    constant CullingUniforms& uniforms [[buffer(0)]],
    const device InstanceData* instances [[buffer(1)]],
    device uint* visibleInstances [[buffer(2)]],
    device atomic_uint* instanceCount [[buffer(3)]],
    uint id [[thread_position_in_grid]]
) {
    if (id >= uniforms.totalInstances) {
        return;
    }
    
    InstanceData inst = instances[id];
    float3 worldPos = inst.position;
    
    // Calculate bounding sphere radius
    float maxScale = max(max(inst.scale.x, inst.scale.y), inst.scale.z);
    float effectiveRadius = inst.cullingRadius * maxScale;
    
    // Frustum culling
    if (isVisible(worldPos, effectiveRadius, uniforms.frustumPlanes)) {
        uint outputIndex = atomic_fetch_add_explicit(instanceCount, 1, memory_order_relaxed);
        visibleInstances[outputIndex] = id;
    }
}

// ============================================================================
// LOD Computation Shader
// ============================================================================

float calculateScreenSize(float3 worldPos, float worldRadius, 
                         float4x4 viewMatrix, float4x4 projectionMatrix,
                         int viewportHeight) {
    // Transform to view space
    float4 viewPos = viewMatrix * float4(worldPos, 1.0);
    float distance = abs(viewPos.z);
    
    if (distance < 0.001) return 1.0;
    
    // Get FOV from projection matrix
    float fovYRad = 2.0 * atan(1.0 / projectionMatrix[1][1]);
    
    // Calculate screen-space size
    float sizeFraction = (worldRadius / distance) / tan(fovYRad * 0.5);
    
    return sizeFraction;
}

kernel void lod_compute(
    constant LODUniforms& uniforms [[buffer(0)]],
    const device InstanceData* sourceInstances [[buffer(1)]],
    device uint* lodAssignments [[buffer(2)]],
    device atomic_uint* lodCounters [[buffer(3)]],
    device uint* lodInstanceIndices [[buffer(4)]],
    uint id [[thread_position_in_grid]]
) {
    if (id >= uniforms.totalInstances) {
        return;
    }
    
    InstanceData inst = sourceInstances[id];
    float3 worldPos = inst.position;
    
    // Calculate effective radius
    float maxScale = max(max(inst.scale.x, inst.scale.y), inst.scale.z);
    float effectiveRadius = inst.cullingRadius * maxScale;
    
    // Frustum culling first
    if (!isVisible(worldPos, effectiveRadius, uniforms.frustumPlanes)) {
        lodAssignments[id] = 3; // Culled
        atomic_fetch_add_explicit(&lodCounters[3], 1, memory_order_relaxed);
        return;
    }
    
    // Calculate distance and screen size
    float distance = length(worldPos - uniforms.cameraPosition);
    float screenSize = calculateScreenSize(worldPos, effectiveRadius,
                                          uniforms.viewMatrix, 
                                          uniforms.projectionMatrix,
                                          uniforms.viewportHeight);
    
    // Determine LOD level
    uint lodLevel;
    
    if (uniforms.lodMode == 0) {
        // Distance-based
        if (distance < uniforms.distanceHigh) {
            lodLevel = 0;
        } else if (distance < uniforms.distanceMedium) {
            lodLevel = 1;
        } else if (distance < uniforms.distanceCull) {
            lodLevel = 2;
        } else {
            lodLevel = 3;
        }
    } else if (uniforms.lodMode == 1) {
        // Screen-space based
        if (screenSize >= uniforms.screenspaceHigh) {
            lodLevel = 0;
        } else if (screenSize >= uniforms.screenspaceMedium) {
            lodLevel = 1;
        } else if (screenSize >= uniforms.screenspaceLow) {
            lodLevel = 2;
        } else {
            lodLevel = 3;
        }
    } else {
        // Hybrid mode
        // Calculate continuous scores and blend
        float distanceScore, screenspaceScore;
        
        if (distance < uniforms.distanceHigh) {
            distanceScore = 0.0;
        } else if (distance < uniforms.distanceMedium) {
            distanceScore = 1.0 + (distance - uniforms.distanceHigh) / 
                          (uniforms.distanceMedium - uniforms.distanceHigh);
        } else if (distance < uniforms.distanceCull) {
            distanceScore = 2.0 + (distance - uniforms.distanceMedium) / 
                          (uniforms.distanceCull - uniforms.distanceMedium);
        } else {
            distanceScore = 3.0;
        }
        
        if (screenSize >= uniforms.screenspaceHigh) {
            screenspaceScore = 0.0;
        } else if (screenSize >= uniforms.screenspaceMedium) {
            screenspaceScore = 1.0 + (uniforms.screenspaceHigh - screenSize) /
                            (uniforms.screenspaceHigh - uniforms.screenspaceMedium);
        } else if (screenSize >= uniforms.screenspaceLow) {
            screenspaceScore = 2.0 + (uniforms.screenspaceMedium - screenSize) /
                            (uniforms.screenspaceMedium - uniforms.screenspaceLow);
        } else {
            screenspaceScore = 3.0;
        }
        
        // Blend scores
        float finalScore = distanceScore * (1.0 - uniforms.hybridWeight) + 
                          screenspaceScore * uniforms.hybridWeight;
        
        // Map to discrete LOD
        if (finalScore < 0.5) {
            lodLevel = 0;
        } else if (finalScore < 1.5) {
            lodLevel = 1;
        } else if (finalScore < 2.5) {
            lodLevel = 2;
        } else {
            lodLevel = 3;
        }
    }
    
    lodAssignments[id] = lodLevel;
    
    if (lodLevel < 3) {
        // Store instance index in LOD-specific buffer
        uint lodLocalIndex = atomic_fetch_add_explicit(&lodCounters[lodLevel], 1, memory_order_relaxed);
        uint baseOffset = lodLevel * uniforms.totalInstances;
        lodInstanceIndices[baseOffset + lodLocalIndex] = id;
    } else {
        atomic_fetch_add_explicit(&lodCounters[3], 1, memory_order_relaxed);
    }
}
