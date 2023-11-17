#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform UniformBufferMVP
{
    mat4 model;
	mat4 view;
	mat4 projection;
} ubMVP;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in uint inMaterialID;

layout(location = 5) in vec3 inWorldPos;
 
out gl_PerVertex
{
    vec4 gl_Position;
};

void main() 
{
	vec4 worldPos = ubMVP.model * vec4(inPosition, 1.0) + vec4(inWorldPos, 0.0);
	vec4 viewPos = ubMVP.view * worldPos;

    gl_Position = ubMVP.projection * viewPos;
} 
