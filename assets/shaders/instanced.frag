#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec4 Color;
in float TextureIndex;
in float LODAlpha;

uniform sampler2DArray uTextureArray;
uniform int useTextureArray;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform float uTime;
uniform int uDitherEnabled;

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

  // Standard lighting
  vec3 lightColor = vec3(1.0);
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

  vec4 texColor;
  if (useTextureArray == 1) {
    texColor = texture(uTextureArray, vec3(TexCoord, TextureIndex));
  } else {
    texColor = Color;
  }

  vec3 result = (ambient + diffuse + specular) * texColor.rgb * Color.rgb;
  FragColor = vec4(result, texColor.a * Color.a);
}
