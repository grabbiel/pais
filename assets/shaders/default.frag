#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec4 Color;

uniform sampler2D uTexture;
uniform int useTexture;
uniform vec4 materialColor;
uniform vec3 lightPos;
uniform vec3 viewPos;

void main() {
  vec3 lightColor = vec3(1.0, 1.0, 1.0);
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

  vec3 result = (ambient + diffuse + specular) * texColor.rgb;
  FragColor = vec4(result, texColor.a);
}
