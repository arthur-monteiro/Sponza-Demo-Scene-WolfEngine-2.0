#version 450
#extension GL_ARB_separate_shader_objects : enable

const uint MAX_MODELS = 2;
layout(binding = 0) uniform UniformBufferMVP
{
    mat4 models[MAX_MODELS];
	mat4 view;
	mat4 projection;
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

const mat4 biasMat = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 );

void main() 
{
	uint modelIdx = inMaterialID >= 24 ? 1 : 0;

	vec4 viewPos = ubMVP.view * ubMVP.models[modelIdx] * vec4(inPosition, 1.0);

    gl_Position = ubMVP.projection * viewPos;

	mat3 usedModelMatrix = transpose(inverse(mat3(ubMVP.view * ubMVP.models[modelIdx])));
    vec3 n = normalize(usedModelMatrix * inNormal);
	vec3 t = normalize(usedModelMatrix * inTangent);
	t = normalize(t - dot(t, n) * n);
	vec3 b = normalize(cross(t, n));
	outTBN = inverse(mat3(t, b, n));

	outViewPos = viewPos.xyz;
    outTexCoord = inTexCoord;
	outMaterialID = inMaterialID;
	outWorldSpaceNormal = normalize(inNormal);
	outWorldSpacePos =  (ubMVP.models[modelIdx] * vec4(inPosition, 1.0)).xyz;
} 
