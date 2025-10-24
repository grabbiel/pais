#version 450 core

layout (location = 0) in vec2 TexCoord;
layout (location = 1) in float TextureIndex;
layout (location = 2) in float VertexAlpha;

layout (binding = 0) uniform sampler2DArray uTextureArray;
layout (binding = 1) uniform sampler2D uTexture;
uniform int useTextureArray;
uniform int useTexture;
uniform float alphaCutoff;
uniform float baseAlpha;

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
