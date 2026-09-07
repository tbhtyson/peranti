#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : enable

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outLightVec;
layout(location = 3) out vec3 outViewVec;
layout(location = 4) flat out uint outInstanceIndex;

layout(buffer_reference, scalar) readonly buffer ShaderData {
    mat4 projection;
    mat4 view;
    mat4 model[3];
    vec4 lightPos;
    uint selected;
};

layout(push_constant) uniform PushConstants {
    ShaderData data;
} pc;

void main() {
    mat4 modelMat = pc.data.model[gl_InstanceIndex];

    outNormal = mat3(pc.data.view * modelMat) * inNormal;
    outUV = inUV;
    gl_Position = pc.data.projection * pc.data.view * modelMat * vec4(inPos, 1.0);

    outInstanceIndex = gl_InstanceIndex;

    vec4 fragPos = pc.data.view * modelMat * vec4(inPos, 1.0);
    outLightVec = pc.data.lightPos.xyz - fragPos.xyz;
    outViewVec = -fragPos.xyz;
}
