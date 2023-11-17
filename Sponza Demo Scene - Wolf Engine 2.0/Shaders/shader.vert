#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0, set = 0) uniform UniformBufferMVP
{
    mat4 model;
} ubMVP;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in uint inMaterialID;

layout(location = 0) out vec3 outViewPos;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out uint outMaterialID;
layout(location = 3) out mat3 outTBN;
layout(location = 6) out vec3 outWorldSpaceNormal;
layout(location = 7) out vec3 outWorldSpacePos;
 
out gl_PerVertex
{
    vec4 gl_Position;
};

void main() 
{
	vec4 viewPos = getViewMatrix() * ubMVP.model * vec4(inPosition, 1.0);

    gl_Position = getProjectionMatrix() * viewPos;
	gl_Position.xy += getCameraJitter() * gl_Position.w;

	mat3 usedModelMatrix = transpose(inverse(mat3(getViewMatrix() * ubMVP.model)));
    vec3 n = normalize(usedModelMatrix * inNormal);
	vec3 t = normalize(usedModelMatrix * inTangent);
	t = normalize(t - dot(t, n) * n);
	vec3 b = normalize(cross(t, n));
	outTBN = inverse(mat3(t, b, n));

	outViewPos = viewPos.xyz;
    outTexCoord = inTexCoord;
	outMaterialID = inMaterialID;
	outWorldSpaceNormal = normalize(inNormal);
} 
