layout (binding = 6, rg32f) uniform image2D denoisingSamplingPattern;

const uint KERNEL_RADIUS = 5;
const uint DENOISE_TEXTURE_SIZE = 25;

float linearizeDepth(float d)
{
    return ubLighting.near * ubLighting.far / (ubLighting.far - d * (ubLighting.far - ubLighting.near));
}

float computeShadows(vec3 normal)
{
    vec2 currentPixelCoords = vec2(gl_FragCoord.x, gl_FragCoord.y );

    float refPixelDepth = linearizeDepth(texture(sampler2D(depthTexture, textureSampler), vec2(currentPixelCoords.x / float(ubLighting.outputSize.x), currentPixelCoords.y / float(ubLighting.outputSize.y))).r);

    uint nSamples = 0;
    float sumShadows = 0.0f;
    for(int i = 0; i < DENOISE_TEXTURE_SIZE; ++i)
    {
        vec2 patternOffset = imageLoad(denoisingSamplingPattern, ivec2(i, 0)).rg;
        vec2 coordsXY = gl_FragCoord.xy + patternOffset;

        float depth = linearizeDepth(texture(sampler2D(depthTexture, textureSampler), vec2(coordsXY.x / float(ubLighting.outputSize.x), coordsXY.y / float(ubLighting.outputSize.y))).r);
        if(abs(depth - refPixelDepth) < 0.5)
        {
            sumShadows += imageLoad(shadowMask, ivec2(coordsXY)).r;
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