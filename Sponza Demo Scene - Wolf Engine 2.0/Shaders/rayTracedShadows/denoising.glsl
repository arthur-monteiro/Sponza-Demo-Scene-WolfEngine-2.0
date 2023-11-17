const uint DENOISE_TEXTURE_SIZE = 25;
const float DEPTH_DIFFERENCE_SCALE = 10.0; // lower = more tolerant
const float DISTANCE_DIFFERENCE_SCALE = 1.0; // lower = more tolerant

vec2 samplesTexturePos[DENOISE_TEXTURE_SIZE];
float sampleWeight[DENOISE_TEXTURE_SIZE];

float computeSampleWeight(vec3 refWorldPos, float samplePlaneDepth, vec2 sampleTexturePos, vec3 sampleWorldPos)
{
#ifdef COMPUTE_SHADOWS
        float sampleDepth = linearizeDepth(texture(sampler2D(depthTexture, textureSampler), sampleTexturePos).r);
#else
        float sampleDepth = linearizeDepth(texelFetch(depthImage, ivec2(sampleTexturePos * ub.outputImageSize), 0).r);
#endif

        //float depthDifferenceScale = smoothstep(0.0, 1.0, DEPTH_DIFFERENCE_SCALE * (1.0 - 100.0f * max(dFdy(inViewPos.z), dFdx(inViewPos.z))));
        float depthDifferenceWeight = 1.0f - smoothstep(0.0, 1.0, DEPTH_DIFFERENCE_SCALE * abs(sampleDepth - samplePlaneDepth));
        float distanceDifferenceWeight = 1.0f - smoothstep(0.0, 1.0, DISTANCE_DIFFERENCE_SCALE * distance(refWorldPos, sampleWorldPos));

        return depthDifferenceWeight * distanceDifferenceWeight;
}

void computeSamplePositions(vec3 worldSpaceNormal, vec3 refWorldPos, mat4 invView, mat4 view, mat4 projection)
{
    vec3 normal = worldSpaceNormal;
    vec3 precomputeSamplesNormal = vec3(0, 0, 1);
    float cosTheta = normal.z;
    mat3 rotMatrix = mat3(1.0f);

    if(abs(cosTheta) < 0.9f)
    {
        float sinTheta = sqrt(normal.x * normal.x + normal.y * normal.y);

        vec3 R = normalize(cross(normal, precomputeSamplesNormal));
        float oneMinCosTheta = 1 - cosTheta;

        rotMatrix = mat3(cosTheta + (R.x * R.x) * oneMinCosTheta,        R.x * R.y * oneMinCosTheta - R.z * sinTheta,    R.x * R.z * oneMinCosTheta + R.y * sinTheta,
                         R.y * R.x * oneMinCosTheta + R.z * sinTheta,    cosTheta + (R.y * R.y) * oneMinCosTheta,        R.y * R.z * oneMinCosTheta - R.x * sinTheta,
                         R.z * R.x * oneMinCosTheta - R.y * sinTheta,    R.z * R.y * oneMinCosTheta + R.x * sinTheta,    cosTheta + (R.z * R.z) * oneMinCosTheta);
    }

    for(int i = 0; i < DENOISE_TEXTURE_SIZE; ++i)
    {
        vec3 patternOffset = rotMatrix * (vec3(imageLoad(denoisingSamplingPattern, ivec2(i, 0)).rg, 0.0f) * 10.0f);
        vec3 sampleWorldPos = refWorldPos + patternOffset;

        vec4 sampleViewPos = view * vec4(sampleWorldPos, 1.0f);
        vec4 sampleClipPos = projection * sampleViewPos;
        sampleClipPos /= sampleClipPos.w;

        vec2 sampleTexturePos = 0.5f * (sampleClipPos.xy + 1.0f);
        samplesTexturePos[i] = sampleTexturePos;

        sampleWeight[i] = computeSampleWeight(refWorldPos, linearizeDepth(sampleClipPos.z), sampleTexturePos, sampleWorldPos);
    }
}

#ifdef COMPUTE_SHADOWS
float computeShadows()
{
    //return imageLoad(shadowMask, ivec2(gl_FragCoord.xy)).r;

    vec3 refWorldPos = (getInvViewMatrix() * vec4(inViewPos, 1.0f)).xyz;

    float totalWeight = 0.0;
    float sumShadows = 0.0;

    // First step: screen space blur
    for(int offsetX = -3; offsetX <= 3; offsetX++)
    {
        for(int offsetY = -3; offsetY <= 3; offsetY++)
        {
            vec2 pixelCoords = gl_FragCoord.xy + vec2(offsetX, offsetY);
            vec2 pixelUV = pixelCoords / float(ubLighting.outputSize.x);
            vec2 clip = pixelUV * 2.0 - 1.0;
            vec4 viewRay = getInvProjectionMatrix() * vec4(clip.x, clip.y, 1.0, 1.0);
            float depth = texture(sampler2D(depthTexture, textureSampler), pixelUV).r;
            float linearDepth = linearizeDepth(depth);
            vec3 viewPos = viewRay.xyz * linearDepth;
            vec3 sampleWorldPos = (getInvViewMatrix() * vec4(viewPos, 1.0f)).xyz;

            float sampleWeight = 0.5; //computeSampleWeight(refWorldPos, linearDepth, pixelUV, sampleWorldPos);
            sumShadows += imageLoad(shadowMask, ivec2(pixelCoords)).r * sampleWeight;
            totalWeight += sampleWeight;
        }
    }

    // Second step: world space blur
    computeSamplePositions(inWorldSpaceNormal, refWorldPos, getInvViewMatrix(), getViewMatrix(), getProjectionMatrix());

    for(int i = 0; i < DENOISE_TEXTURE_SIZE; ++i)
    {
        vec2 sampleTexturePos = samplesTexturePos[i];
        float weight = sampleWeight[i];

        sumShadows += imageLoad(shadowMask, ivec2(sampleTexturePos * vec2(ubLighting.outputSize))).r * weight;
        totalWeight += weight; 
    }

    return sumShadows / totalWeight;
}
#endif