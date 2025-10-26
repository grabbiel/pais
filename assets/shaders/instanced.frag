#version 450 core

layout (location = 0) out vec4 FragColor;

layout (location = 0) in vec3 FragPos;
layout (location = 1) in vec3 Normal;
layout (location = 2) in vec2 TexCoord;
layout (location = 3) in vec4 Color;
layout (location = 4) in float TextureIndex;
layout (location = 5) in float LODAlpha;
layout (location = 6) in vec4 FragPosLightSpace;

layout(set = 0, binding = 0) uniform sampler2DArray uTextureArray;
layout(set = 0, binding = 2) uniform sampler2DShadow shadowMap;
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

float getBayerValue(vec2 pos) {
  int x = int(mod(pos.x, 4.0));
  int y = int(mod(pos.y, 4.0));

  float bayer[16] = float[16](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0, 4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0, 7.0/16.0, 13.0/16.0,  5.0/16.0
  );

  return bayer[y * 4 + x];
}

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
  // Dithered LOD transition
  if (uDitherEnabled > 0 && LODAlpha < 1.0) {
    float threshold = getBayerValue(gl_FragCoord.xy);

    // Temporal jitter for animated dither
    if (uDitherEnabled > 1) {
      float jitter = fract(uTime * 0.5);
      threshold = fract(threshold + jitter);
    }

    if (LODAlpha < threshold) {
      discard;
    }
  }

  float lightIntensity = lightingParams.x;
  float ambientStrength = lightingParams.y;
  float roughness = clamp(materialParams.x, 0.02, 1.0);
  float metallic = clamp(materialParams.y, 0.0, 1.0);
  float glareStrength = max(materialParams.z, 0.0);

  vec3 norm = normalize(Normal);
  vec3 lightDir = normalize(lightPos - FragPos);
  vec3 viewDir = normalize(viewPos - FragPos);
  vec3 reflectDir = reflect(-lightDir, norm);

  float diff = max(dot(norm, lightDir), 0.0);
  vec3 diffuse = diff * lightColor * lightIntensity;

  float shininess = mix(8.0, 128.0, 1.0 - roughness);
  float spec_angle = max(dot(viewDir, reflectDir), 0.0);
  float spec = pow(spec_angle, shininess);
  float specularStrength = mix(0.1, 1.0, metallic);
  vec3 specular = specularStrength * spec * lightColor * lightIntensity;

  vec3 ambient = ambientStrength * lightColor * lightIntensity;

  vec4 sampledColor = vec4(1.0);
  if (useTextureArray == 1) {
    sampledColor = texture(uTextureArray, vec3(TexCoord, TextureIndex));
  }

  vec4 baseColor = sampledColor * materialColor * Color;

  float shadowFactor = shadowsEnabled == 1 ? calculateShadow(FragPosLightSpace) : 1.0;
  vec3 lighting = ambient + (diffuse + specular) * shadowFactor;
  vec3 result = lighting * baseColor.rgb;

  vec3 glare = glareStrength * spec * lightColor;
  result += glare;

  FragColor = vec4(result, baseColor.a);
}
