layout (binding = 6, rg32f) uniform image2D denoisingSamplingPattern;

//const uint KERNEL_RADIUS = 5;
const uint DENOISE_TEXTURE_SIZE = 25;

const uint MAX_MODELS = 2;
layout(binding = 0) uniform UniformBufferMVP
{
    mat4 models[MAX_MODELS];
	mat4 view;
	mat4 projection;
} ubMVP;

float linearizeDepth(float d)
{
    return ubLighting.near * ubLighting.far / (ubLighting.far - d * (ubLighting.far - ubLighting.near));
}

vec3 viewPosFromDepth(vec2 screenSpaceUV)
{
    vec2 d = screenSpaceUV * 2.0f - 1.0f;
    vec4 viewRay = ubLighting.invProjection * vec4(d.x, d.y, 1.0, 1.0);
    float linearDepth = linearizeDepth(texture(sampler2D(depthTexture, textureSampler), screenSpaceUV).r);

    return viewRay.xyz * linearDepth;
}

float computeShadows()
{
    //return imageLoad(shadowMask, ivec2(gl_FragCoord.xy)).r;

    vec3 normal = inWorldSpaceNormal;
    vec3 precomputeSamplesNormal = vec3(0, 0, 1);
    float cosTheta = normal.z;
    mat3 rotMatrix = mat3(1.0f);

    if(abs(cosTheta) < 0.9f)
    {
        float sinTheta = sqrt(normal.x * normal.x + normal.y * normal.y);

        vec3 R = normalize(cross(normal, precomputeSamplesNormal));
        float oneMinCosTheta = 1 - cosTheta;

        rotMatrix = mat3(cosTheta + (R.x * R.x) * oneMinCosTheta,        R.x * R.y * oneMinCosTheta - R.z * sinTheta,    R.x * R.z * oneMinCosTheta + R.y * sinTheta,
                            R.y * R.x * oneMinCosTheta + R.y * sinTheta,    cosTheta + (R.y * R.y) * oneMinCosTheta,        R.y * R.z * oneMinCosTheta - R.x * sinTheta,
                            R.z * R.x * oneMinCosTheta - R.y * sinTheta,    R.z * R.y * oneMinCosTheta + R.x * sinTheta,    cosTheta + (R.z * R.z) * oneMinCosTheta);
    }

    vec3 refWorldPos = (ubLighting.invView * vec4(inViewPos, 1.0f)).xyz;

    float refPixelDepth = linearizeDepth(texture(sampler2D(depthTexture, textureSampler), vec2(gl_FragCoord.x / float(ubLighting.outputSize.x), gl_FragCoord.y / float(ubLighting.outputSize.y))).r);

    uint nSamples = 0;
    float sumShadows = 0.0f;
    for(int i = 0; i < DENOISE_TEXTURE_SIZE; ++i)
    {
        vec3 patternOffset = rotMatrix * vec3(imageLoad(denoisingSamplingPattern, ivec2(i, 0)).rg, 0.0f);
        vec3 sampleWorldPos = refWorldPos + patternOffset;

        vec4 sampleViewPos = ubMVP.view * vec4(sampleWorldPos, 1.0f);
        vec4 sampleClipPos = ubMVP.projection * sampleViewPos;
        sampleClipPos /= sampleClipPos.w;

        vec2 sampleTexturePos = 0.5f * (sampleClipPos.xy + 1.0f);

        float depth = linearizeDepth(texture(sampler2D(depthTexture, textureSampler), sampleTexturePos).r);
        if(abs(depth - refPixelDepth) < 0.5)
        {
            sumShadows += imageLoad(shadowMask, ivec2(sampleTexturePos * vec2(ubLighting.outputSize))).r;
            nSamples++; 
        }
    }

    // for(int x = int(currentPixelCoords.x - KERNEL_RADIUS); x < currentPixelCoords.x + KERNEL_RADIUS; ++x)
    // {
    //     for(int y = int(currentPixelCoords.y - KERNEL_RADIUS); y < currentPixelCoords.y + KERNEL_RADIUS; ++y)
    //     {
    //         float depth = linearizeDepth(texture(sampler2D(depthTexture, textureSampler), vec2(float(x) / float(ubLighting.outputSize.x), float(y) / float(ubLighting.outputSize.y))).r);

    //         if(abs(depth - refPixelDepth) < 0.5)
    //         {
    //             sumShadows += imageLoad(shadowMask, ivec2(x, y)).r;
    //             nSamples++;
    //         }
    //     }
    // }

    return sumShadows / float(nSamples);
}