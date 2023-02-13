#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(early_fragment_tests) in;

layout(location = 0) in vec3 inViewPos;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) flat in uint inMaterialID;
layout(location = 3) in mat3 inTBN;

layout(binding = 1) uniform sampler textureSampler;
layout(binding = 2) uniform texture2D[] textures;

layout(location = 0) out vec4 outColor;

#include "ShaderCommon.glsl"

void main() 
{
	outColor = vec4(sRGBToLinear(texture(sampler2D(textures[inMaterialID * 5], textureSampler), inTexCoord).rgb), 1.0);
}
