#version 450 core

layout (location = 0) in vec2 TexCoord;
layout (location = 1) in float TextureIndex;
layout (location = 2) in float VertexAlpha;

layout(set = 0, binding = 0) uniform sampler2DArray uTextureArray;
layout(set = 0, binding = 2) uniform sampler2D uTexture;
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
  float alpha = baseAlpha * VertexAlpha;

  if (useTextureArray == 1) {
    alpha *= texture(uTextureArray, vec3(TexCoord, TextureIndex)).a;
  } else if (useTexture == 1) {
    alpha *= texture(uTexture, TexCoord).a;
  }

  if (alpha < alphaCutoff) {
    discard;
  }
}
