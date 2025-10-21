# Shader Files

This directory contains shader files for the Pixel renderer. The renderer supports loading shaders from disk using the `Renderer::load_shader()` method.

## Shader Types

### GLSL Shaders (.vert, .frag)

GLSL (OpenGL Shading Language) shaders are used by the OpenGL backend.

- **default.vert** / **default.frag** - Basic vertex and fragment shaders with:
  - MVP (Model-View-Projection) transformations
  - Phong lighting model
  - Texture support
  - Per-vertex colors and normals

- **instanced.vert** / **instanced.frag** - Instanced rendering shaders with:
  - Per-instance transformations (position, rotation, scale)
  - Per-instance colors and texture indices
  - Dithered LOD (Level of Detail) transitions
  - Texture array support

### Metal Shaders (.metal)

Metal Shading Language shaders are used by the Metal backend on Apple platforms.

- **basic.metal** - Example Metal shader with:
  - Standard vertex and fragment shader functions
  - Phong lighting
  - Texture sampling
  - Uniform buffer support

## Loading Shaders

### From Files

```cpp
// Load shaders from disk
ShaderID shader = renderer.load_shader(
    "assets/shaders/default.vert",
    "assets/shaders/default.frag"
);
```

### From Source Strings

```cpp
// Load shaders from source code strings
std::string vert_src = "...";
std::string frag_src = "...";
ShaderID shader = renderer.create_shader_from_source(vert_src, frag_src);
```

## Shader Features

### Default Shaders

The default shaders support:
- Standard mesh rendering
- Phong lighting (ambient + diffuse + specular)
- Optional texture mapping
- Material colors
- Per-vertex colors

### Instanced Shaders

The instanced shaders support:
- Efficient rendering of many instances
- Per-instance transformations
- LOD system with smooth transitions
- Texture arrays
- Dithering for LOD transitions

## Vertex Attributes

All shaders expect the following vertex attributes:

| Location | Type  | Name      | Description           |
|----------|-------|-----------|----------------------|
| 0        | vec3  | aPos      | Vertex position      |
| 1        | vec3  | aNormal   | Vertex normal        |
| 2        | vec2  | aTexCoord | Texture coordinates  |
| 3        | vec4  | aColor    | Vertex color         |

### Additional Instance Attributes

Instanced shaders also use:

| Location | Type  | Name            | Description              |
|----------|-------|-----------------|-------------------------|
| 4        | vec3  | iPosition       | Instance position       |
| 5        | vec3  | iRotation       | Instance rotation       |
| 6        | vec3  | iScale          | Instance scale          |
| 7        | vec4  | iColor          | Instance color          |
| 8        | float | iTextureIndex   | Texture array index     |
| 9        | float | iLODAlpha       | LOD transition alpha    |

## Uniforms

### Standard Uniforms

```glsl
uniform mat4 model;          // Model transformation matrix
uniform mat4 view;           // View/camera matrix
uniform mat4 projection;     // Projection matrix
uniform vec3 lightPos;       // Light position in world space
uniform vec3 viewPos;        // Camera position in world space
uniform sampler2D uTexture;  // Texture sampler
uniform int useTexture;      // Flag to enable/disable texture
uniform vec4 materialColor;  // Material base color
```

### Instanced Uniforms

```glsl
uniform mat4 view;                    // View matrix
uniform mat4 projection;              // Projection matrix
uniform vec3 lightPos;                // Light position
uniform vec3 viewPos;                 // Camera position
uniform sampler2DArray uTextureArray; // Texture array
uniform int useTextureArray;          // Flag to enable texture array
uniform float uTime;                  // Current time for animations
uniform int uDitherEnabled;           // LOD dithering mode
```

## Writing Custom Shaders

When creating custom shaders, ensure:

1. **Version compatibility**: Use `#version 330 core` for GLSL
2. **Attribute locations**: Match the vertex attribute layout
3. **Uniform names**: Use the expected uniform names for built-in features
4. **File extensions**: Use `.vert` for vertex shaders, `.frag` for fragment shaders, `.metal` for Metal shaders
5. **Shader paths**: Place shaders in `assets/shaders/` for proper loading
