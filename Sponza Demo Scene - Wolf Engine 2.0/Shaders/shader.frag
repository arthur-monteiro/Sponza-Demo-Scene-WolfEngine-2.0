#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout (early_fragment_tests) in;

layout (location = 0) in vec3 inViewPos;
layout (location = 1) in vec2 inTexCoords;
layout (location = 2) flat in uint inMaterialID;
layout (location = 3) in mat3 inTBN;
layout (location = 6) in vec3 inWorldSpaceNormal;
layout (location = 7) in vec3 inWorldSpacePos;

layout(binding = 0, set = 0) uniform UniformBufferMVP
{
    mat4 model;
} ubMVP;
layout (binding = 3, set = 3, r32f) uniform image2D shadowMask;

layout(binding = 4, set = 3, std140) uniform readonly UniformBufferLighting
{
	vec3 directionDirectionalLight;

	vec3 colorDirectionalLight;

    uvec2 outputSize;
} ubLighting;

layout (binding = 0, set = 2) uniform texture2D[] textures;
layout (binding = 1, set = 2) uniform sampler textureSampler;

#if RAYTRACED_SHADOWS
layout (binding = 5, set = 3) uniform texture2D depthTexture;
layout (binding = 6, set = 3, rg32f) uniform image2D denoisingSamplingPattern;
#endif

layout (location = 0) out vec4 outColor;

#include "ShaderCommon.glsl"

#if RAYTRACED_SHADOWS
vec3 viewPosFromDepth(vec2 screenSpaceUV)
{
    vec2 d = screenSpaceUV * 2.0f - 1.0f;
    vec4 viewRay = getInvProjectionMatrix() * vec4(d.x, d.y, 1.0, 1.0);
    float linearDepth = linearizeDepth(texture(sampler2D(depthTexture, textureSampler), screenSpaceUV).r);

    return viewRay.xyz * linearDepth;
}

#define COMPUTE_SHADOWS
#include "rayTracedShadows/denoising.glsl"
#endif

const mat4 biasMat = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 );

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness);
float GeometrySchlickGGX(float NdotV, float roughness);
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness);
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness);

void main() 
{
	vec3 albedo = texture(sampler2D(textures[inMaterialID * 5     + 0], textureSampler), inTexCoords).rgb;
	vec3 normal = (texture(sampler2D(textures[inMaterialID * 5    + 1], textureSampler), inTexCoords).rgb * 2.0 - vec3(1.0)) * inTBN;
	float roughness = texture(sampler2D(textures[inMaterialID * 5 + 2], textureSampler), inTexCoords).r;
	float metalness = texture(sampler2D(textures[inMaterialID * 5 + 3], textureSampler), inTexCoords).r;
    normal = normalize(normal);

#if RAYTRACED_SHADOWS
    float shadow = computeShadows();
#else
	float shadow = imageLoad(shadowMask, ivec2(gl_FragCoord.xy)).r;
#endif

	vec3 V = normalize(-inViewPos);
    vec3 R = reflect(-V, normal);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0,albedo, metalness);

    vec3 Lo = vec3(0.0);

    // calculate per-light radiance
    vec3 L = normalize(-ubLighting.directionDirectionalLight.xyz);
    vec3 H = normalize(V + L);
    vec3 radiance = ubLighting.colorDirectionalLight.xyz;

    // cook-torrance brdf
    float NDF = DistributionGGX(normal, H, roughness);
    float G   = GeometrySmith(normal, V, L, roughness);
    vec3 F    = fresnelSchlickRoughness(max(dot(H, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metalness;

    vec3 nominator    = NDF * G * F;
    float denominator = 4 * max(dot(normal, V), 0.0) * max(dot(normal, L), 0.0);
    vec3 specular     = nominator / max(denominator, 0.001);

    // add to outgoing radiance Lo
    float NdotL = max(dot(normal, L), 0.0);
    Lo += (kD * albedo / PI + specular) * radiance * NdotL;

	vec3 color = Lo * shadow + albedo * 0.025;

    // Tone mapping
    float exposure = 2.0;
    color = color / (color + 1.0 / exposure);
    color = color * color;
	//color = vec3(1.0) - exp(-color * 0.5);
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}
