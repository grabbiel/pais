#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aColor;

layout (location = 0) out vec2 TexCoord;
layout (location = 1) out float VertexAlpha;

layout(std140, set = 0, binding = 1) uniform PixelUniforms {
  mat4 model;
  mat4 view;
  mat4 projection;
  mat4 normalMatrix;
  mat4 lightViewProj;
  vec4 materialColor;
  vec3 lightPos;
  float alphaCutoff;
  vec3 viewPos;
  float baseAlpha;
  vec3 lightColor;
  float shadowBias;
  float uTime;
  float ditherScale;
  float crossfadeDuration;
  float _padMisc;
  vec4 lightingParams;
  vec4 materialParams;
  int useTexture;
  int useTextureArray;
  int uDitherEnabled;
  int shadowsEnabled;
};

void main() {
  TexCoord = aTexCoord;
  VertexAlpha = aColor.a;
  gl_Position = lightViewProj * model * vec4(aPos, 1.0);
}
