#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferMVP
{
    mat4 mvp;
} ubMVP;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in uint inMaterialID;
 
out gl_PerVertex
{
    vec4 gl_Position;
};
void main() 
{
	gl_Position = ubMVP.mvp * vec4(inPosition, 1.0);
} 