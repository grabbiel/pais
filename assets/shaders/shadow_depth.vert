#version 450 core
layout (location = 0) in vec3 aPos;

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
  int useTexture;
  int useTextureArray;
  int uDitherEnabled;
  int shadowsEnabled;
};

void main() {
  gl_Position = lightViewProj * model * vec4(aPos, 1.0);
}
