# RHI Uniform Buffer Support for Instanced Rendering

## Overview

The RHI now supports uniform buffer objects (UBOs) for efficient instanced rendering. This allows you to pass large amounts of uniform data to shaders without individual uniform calls.

## API

### CmdList::setUniformBuffer

```cpp
virtual void setUniformBuffer(uint32_t binding, BufferHandle buffer,
                              size_t offset = 0, size_t size = 0) = 0;
```

**Parameters:**
- `binding`: The uniform buffer binding point (matches shader `layout(binding = N)`)
- `buffer`: Handle to a buffer created with `BufferUsage::Uniform`
- `offset`: Optional byte offset into the buffer (default: 0)
- `size`: Optional size in bytes to bind. If 0, binds entire buffer (default: 0)

## Usage Example

### 1. Create a Uniform Buffer

```cpp
// Define your uniform data structure (must match shader layout)
struct InstanceUniforms {
    alignas(16) float modelMatrix[16];
    alignas(16) float color[4];
    float textureIndex;
    float padding[3]; // Ensure proper alignment
};

// Create buffer
rhi::BufferDesc desc{};
desc.size = sizeof(InstanceUniforms) * MAX_INSTANCES;
desc.usage = rhi::BufferUsage::Uniform;
desc.hostVisible = true; // Allow CPU updates

rhi::BufferHandle uniformBuffer = device->createBuffer(desc);
```

### 2. Upload Data to the Buffer

```cpp
std::vector<InstanceUniforms> instanceData(instanceCount);

// Fill instance data
for (size_t i = 0; i < instanceCount; i++) {
    // Set model matrix, color, etc.
    instanceData[i].textureIndex = i % textureCount;
}

// Copy to GPU
cmd->copyToBuffer(
    uniformBuffer,
    0,
    std::as_bytes(std::span(instanceData))
);
```

### 3. Bind Uniform Buffer in Render Loop

```cpp
cmd->begin();
cmd->beginRender(colorTarget, depthTarget, clearColor, 1.0f, 0);
cmd->setPipeline(pipeline);
cmd->setVertexBuffer(vertexBuffer);
cmd->setIndexBuffer(indexBuffer);

// Bind uniform buffer to binding point 0
cmd->setUniformBuffer(0, uniformBuffer);

// Draw instances
cmd->drawIndexed(indexCount, 0, instanceCount);

cmd->endRender();
cmd->end();
```

### 4. Shader Setup (GLSL)

```glsl
// Vertex shader
#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;

// Uniform buffer block at binding 0
layout(std140, binding = 0) uniform InstanceData {
    mat4 modelMatrix;
    vec4 color;
    float textureIndex;
} instance;

uniform mat4 viewProjection;

out vec3 fragNormal;
out vec2 fragTexCoord;
out vec4 fragColor;
out float fragTextureIndex;

void main() {
    gl_Position = viewProjection * instance.modelMatrix * vec4(aPosition, 1.0);
    fragNormal = mat3(instance.modelMatrix) * aNormal;
    fragTexCoord = aTexCoord;
    fragColor = instance.color * aColor;
    fragTextureIndex = instance.textureIndex;
}
```

## Dynamic Uniform Buffer Binding

For better performance with many instances, you can bind different ranges of the same buffer:

```cpp
// Bind range for instance 0
cmd->setUniformBuffer(0, uniformBuffer, 0, sizeof(InstanceUniforms));

// Draw instance 0
cmd->drawIndexed(indexCount, 0, 1);

// Bind range for instance 1
cmd->setUniformBuffer(0, uniformBuffer, sizeof(InstanceUniforms), sizeof(InstanceUniforms));

// Draw instance 1
cmd->drawIndexed(indexCount, 0, 1);
```

## OpenGL Backend Implementation

The OpenGL backend uses:
- `glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer)` when size is 0
- `glBindBufferRange(GL_UNIFORM_BUFFER, binding, buffer, offset, size)` when size > 0

This provides efficient uniform buffer binding for modern OpenGL (3.3+).

## Benefits

1. **Performance**: Single buffer bind instead of multiple uniform calls
2. **Flexibility**: Can update subranges without rebinding entire buffer
3. **Instancing**: Ideal for per-instance data in instanced rendering
4. **Standard**: Uses OpenGL UBO standard with `std140` layout
