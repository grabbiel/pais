#version 450 core

layout (location = 0) out vec4 FragColor;

layout (location = 0) in vec3 FragPos;
layout (location = 1) in vec3 Normal;
layout (location = 2) in vec2 TexCoord;
layout (location = 3) in vec4 Color;
layout (location = 4) in vec4 FragPosLightSpace;

uniform sampler2D uTexture;
uniform sampler2DShadow shadowMap;
uniform int useTexture;
uniform vec4 materialColor;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform float shadowBias;
uniform int shadowsEnabled;

float calculateShadow(vec4 fragPosLightSpace) {
  vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
  projCoords = projCoords * 0.5 + 0.5;
  if (projCoords.z > 1.0) {
    return 1.0;
  }

  vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
  float visibility = 0.0;
  for (int x = -1; x <= 1; ++x) {
    for (int y = -1; y <= 1; ++y) {
      vec3 sampleCoord = vec3(projCoords.xy + vec2(x, y) * texelSize,
                              projCoords.z - shadowBias);
      visibility += texture(shadowMap, sampleCoord);
    }
  }
  visibility /= 9.0;
  return visibility;
}

void main() {
  float ambientStrength = 0.3;
  vec3 ambient = ambientStrength * lightColor;

  vec3 norm = normalize(Normal);
  vec3 lightDir = normalize(lightPos - FragPos);
  float diff = max(dot(norm, lightDir), 0.0);
  vec3 diffuse = diff * lightColor;

  vec3 viewDir = normalize(viewPos - FragPos);
  vec3 reflectDir = reflect(-lightDir, norm);
  float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
  vec3 specular = 0.5 * spec * lightColor;

  // Use materialColor with vertex color
  vec4 baseColor = materialColor * Color;
  vec4 texColor = (useTexture == 1) ? texture(uTexture, TexCoord) * baseColor : baseColor;

  float shadowFactor = shadowsEnabled == 1 ? calculateShadow(FragPosLightSpace) : 1.0;
  vec3 result = ambient + (diffuse + specular) * shadowFactor;
  result *= texColor.rgb;
  FragColor = vec4(result, texColor.a);
}
