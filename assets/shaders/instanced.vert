#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aColor;

// Instance attributes
layout (location = 4) in vec3 iPosition;
layout (location = 5) in vec3 iRotation;
layout (location = 6) in vec3 iScale;
layout (location = 7) in vec4 iColor;
layout (location = 8) in float iTextureIndex;
layout (location = 9) in float iLODAlpha;

layout (location = 0) out vec3 FragPos;
layout (location = 1) out vec3 Normal;
layout (location = 2) out vec2 TexCoord;
layout (location = 3) out vec4 Color;
layout (location = 4) out float TextureIndex;
layout (location = 5) out float LODAlpha;
layout (location = 6) out vec4 FragPosLightSpace;

uniform mat4 model;
uniform mat4 normalMatrix;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightViewProj;

mat4 rotationMatrix(vec3 axis, float angle) {
  axis = normalize(axis);
  float s = sin(angle);
  float c = cos(angle);
  float oc = 1.0 - c;

  return mat4(
    oc * axis.x * axis.x + c,           oc * axis.x * axis.y - axis.z * s,  oc * axis.z * axis.x + axis.y * s,  0.0,
    oc * axis.x * axis.y + axis.z * s,  oc * axis.y * axis.y + c,           oc * axis.y * axis.z - axis.x * s,  0.0,
    oc * axis.z * axis.x - axis.y * s,  oc * axis.y * axis.z + axis.x * s,  oc * axis.z * axis.z + c,           0.0,
    0.0,                                 0.0,                                 0.0,                                 1.0
  );
}

void main() {
  // Build transformation matrix
  mat4 rotX = rotationMatrix(vec3(1, 0, 0), iRotation.x);
  mat4 rotY = rotationMatrix(vec3(0, 1, 0), iRotation.y);
  mat4 rotZ = rotationMatrix(vec3(0, 0, 1), iRotation.z);
  mat4 rotation = rotZ * rotY * rotX;

  // Apply scale and rotation to position
  vec4 scaledPos = vec4(aPos * iScale, 1.0);
  vec4 rotatedPos = rotation * scaledPos;
  vec4 instanceWorldPos = rotatedPos + vec4(iPosition, 0.0);

  // Apply the shared model transform so Metal and GLSL stay in sync
  vec4 worldPos = model * instanceWorldPos;

  FragPos = worldPos.xyz;
  vec3 transformedNormal = mat3(rotation) * aNormal;
  Normal = mat3(normalMatrix) * transformedNormal;
  TexCoord = aTexCoord;
  Color = aColor * iColor;
  TextureIndex = iTextureIndex;
  LODAlpha = iLODAlpha;
  FragPosLightSpace = lightViewProj * vec4(FragPos, 1.0);

  gl_Position = projection * view * worldPos;
}
