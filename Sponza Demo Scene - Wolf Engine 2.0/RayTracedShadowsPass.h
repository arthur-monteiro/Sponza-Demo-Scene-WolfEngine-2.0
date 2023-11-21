#pragma once

#include <glm/glm.hpp>

#include <CommandRecordBase.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <DescriptorSetLayoutGenerator.h>
#include <Pipeline.h>
#include <ShaderBindingTable.h>
#include <ShaderParser.h>

#include "CameraInterface.h"

namespace Wolf
{
	class TopLevelAccelerationStructure;
}

class PreDepthPass;
class ObjectModel;
class SharedGPUResources;

#include "Sampler.h"
#include "ShadowMaskBasePass.h"

class RayTracedShadowsPass : public Wolf::CommandRecordBase, public ShadowMaskBasePass
{
public:
	RayTracedShadowsPass(const Wolf::TopLevelAccelerationStructure* topLevelAccelerationStructure, PreDepthPass* preDepthPass);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	Wolf::Image* getOutput(uint32_t frameIdx) override { return m_outputMask.get(); }
	const Wolf::Semaphore* getSemaphore() const override { return Wolf::CommandRecordBase::getSemaphore(); }
	void getConditionalBlocksToEnableWhenReadingMask(std::vector<std::string>& conditionalBlocks) const override { conditionalBlocks.emplace_back("RAYTRACED_SHADOWS"); }
	Wolf::Image* getDenoisingPatternImage() override { return m_denoiseSamplingPattern.get(); }
	Wolf::Image* getDebugImage() const override { return m_debugOutputImage.get(); }

	void saveMaskToFile(const std::string& filename) const;

private:
	void createPipelines();
	void createDescriptorSet();
	void createOutputImages(uint32_t width, uint32_t height);

	static float jitter();

private:
	const Wolf::TopLevelAccelerationStructure* m_topLevelAccelerationStructure;
	PreDepthPass* m_preDepthPass;

	std::unique_ptr<Wolf::Pipeline> m_pipeline;

	std::unique_ptr<Wolf::ShaderBindingTable> m_shaderBindingTable;
	std::unique_ptr<Wolf::ShaderParser> m_rayGenShaderParser;
	std::unique_ptr<Wolf::ShaderParser> m_rayMissShaderParser;
	std::unique_ptr<Wolf::ShaderParser> m_closestHitShaderParser;

	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;
	Wolf::DescriptorSetLayoutGenerator m_descriptorSetLayoutGenerator;
	std::unique_ptr<Wolf::DescriptorSet> m_descriptorSet;

	struct ShadowUBData
	{
		glm::vec4 sunDirectionAndNoiseIndex;

		glm::uint drawWithoutNoiseFrameIndex; // 0 = draw with noise
		float sunAreaAngle;
	};
	std::unique_ptr<Wolf::Buffer> m_uniformBuffer;
	std::unique_ptr<Wolf::Image> m_outputMask;

	// Noise
	static constexpr uint32_t NOISE_TEXTURE_SIZE_PER_SIDE = 128;
	static constexpr uint32_t NOISE_TEXTURE_VECTOR_COUNT = 16;
	std::unique_ptr<Wolf::Image> m_noiseImage;
	std::unique_ptr<Wolf::Sampler> m_noiseSampler;

	// Denoise
	static constexpr uint32_t DENOISE_TEXTURE_SIZE = 25;
	std::unique_ptr<Wolf::Image> m_denoiseSamplingPattern;

	// Debug
	std::unique_ptr<Wolf::Image> m_debugOutputImage;
	std::unique_ptr<Wolf::ShaderParser> m_debugComputeShaderParser;

	std::unique_ptr<Wolf::DescriptorSetLayout> m_debugDescriptorSetLayout;
	Wolf::DescriptorSetLayoutGenerator m_debugDescriptorSetLayoutGenerator;
	std::unique_ptr<Wolf::DescriptorSet> m_debugDescriptorSet;
	struct DebugUBData
	{
		glm::vec3 worldSpaceNormal;
		float padding;
		glm::vec2 pixelUV;
		glm::vec2 patternSize;
		glm::vec2 outputImageSize;
	};
	std::unique_ptr<Wolf::Buffer> m_debugUniformBuffer;

	std::unique_ptr<Wolf::Pipeline> m_debugPipeline;
};