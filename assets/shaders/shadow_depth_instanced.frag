#version 330 core

in vec2 TexCoord;
in float TextureIndex;
in float VertexAlpha;

uniform sampler2DArray uTextureArray;
uniform sampler2D uTexture;
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
