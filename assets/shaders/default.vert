#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aColor;

layout (location = 0) out vec3 FragPos;
layout (location = 1) out vec3 Normal;
layout (location = 2) out vec2 TexCoord;
layout (location = 3) out vec4 Color;
layout (location = 4) out vec4 FragPosLightSpace;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightViewProj;

void main() {
  FragPos = vec3(model * vec4(aPos, 1.0));
  Normal = mat3(transpose(inverse(model))) * aNormal;
  TexCoord = aTexCoord;
  Color = aColor;
  FragPosLightSpace = lightViewProj * vec4(FragPos, 1.0);
  gl_Position = projection * view * vec4(FragPos, 1.0);
}
