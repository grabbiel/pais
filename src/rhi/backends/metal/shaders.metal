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

struct InstanceData {
    float3 position      [[attribute(4)]];
    float3 rotation      [[attribute(5)]];
    float3 scale         [[attribute(6)]];
    float4 color         [[attribute(7)]];
    float textureIndex   [[attribute(8)]];
    float lodAlpha       [[attribute(9)]];
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

vertex VertexOutInstanced vertex_instanced(
    VertexIn in [[stage_in]],
    InstanceData instance [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]]
) {
    VertexOutInstanced out;

    // Create instance transform matrix
    float4x4 scaleMatrix = float4x4(
        float4(instance.scale.x, 0.0, 0.0, 0.0),
        float4(0.0, instance.scale.y, 0.0, 0.0),
        float4(0.0, 0.0, instance.scale.z, 0.0),
        float4(0.0, 0.0, 0.0, 1.0)
    );

    float4x4 rotationMatrix = createRotationMatrix(instance.rotation);

    float4x4 translationMatrix = float4x4(
        float4(1.0, 0.0, 0.0, 0.0),
        float4(0.0, 1.0, 0.0, 0.0),
        float4(0.0, 0.0, 1.0, 0.0),
        float4(instance.position.x, instance.position.y, instance.position.z, 1.0)
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
    out.color = in.color * instance.color;

    // Pass instance-specific data
    out.textureIndex = instance.textureIndex;
    out.lodAlpha = instance.lodAlpha;

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
