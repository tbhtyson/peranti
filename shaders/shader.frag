#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inFactor;
layout(location = 3) in vec3 inLightVec;
layout(location = 4) in vec3 inViewVec;
layout(location = 5) flat in uint inInstanceIndex;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D textures[];

void main() {
    vec3 N = normalize(inNormal);
    vec3 L = normalize(inLightVec);
    vec3 V = normalize(inViewVec);
    vec3 R = reflect(-L, N);

    vec3 diffuse  = vec3(max(dot(N, L), 0.0025));
    vec3 specular = vec3(pow(max(dot(R, V), 0.0), 16.0) * 0.75);

    vec3 color = texture(textures[nonuniformEXT(inInstanceIndex)], inUV).rgb * inFactor;

    outColor = vec4(diffuse * color + specular, 1.0);
}
