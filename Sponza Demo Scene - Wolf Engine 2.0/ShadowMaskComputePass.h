#pragma once

#include <glm/glm.hpp>

#include <CommandRecordBase.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <DescriptorSetLayoutGenerator.h>
#include <Pipeline.h>
#include <Sampler.h>
#include <ShaderParser.h>

#include "CascadedShadowMapping.h"
#include "ShadowMaskBasePass.h"

class DepthPass;
class SharedGPUResources;

class ShadowMaskComputePass : public Wolf::CommandRecordBase, public ShadowMaskBasePass
{
public:
	ShadowMaskComputePass(DepthPass* preDepthPass, CascadedShadowMapping* csmManager);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	Wolf::Image* getOutput(uint32_t frameIdx) override { return m_outputMasks[frameIdx % MASK_COUNT].get(); }
	const Wolf::Semaphore* getSemaphore() const override { return Wolf::CommandRecordBase::getSemaphore(); }
	void getConditionalBlocksToEnableWhenReadingMask(std::vector<std::string>& conditionalBlocks) const override {}
	Wolf::Image* getDenoisingPatternImage() override { return nullptr; }

private:
	void createOutputImages(uint32_t width, uint32_t height);
	void createPipeline();
	void updateDescriptorSet() const;

	static float jitter();

private:
	/* Pipeline */
	std::unique_ptr<Wolf::ShaderParser> m_computeShaderParser;
	std::unique_ptr<Wolf::Pipeline> m_pipeline;
	std::array<std::unique_ptr<Wolf::Image>, MASK_COUNT> m_outputMasks;

	/* Resources */
	Wolf::DescriptorSetLayoutGenerator m_descriptorSetLayoutGenerator;
	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;
	std::array<std::unique_ptr<Wolf::DescriptorSet>, MASK_COUNT> m_descriptorSets;
	std::array<float, 16> m_noiseRotations;
	DepthPass* m_preDepthPass;
	CascadedShadowMapping* m_csmManager;
	struct ShadowUBData
	{
		glm::mat4 invView;

		glm::mat4 invProjection;

		glm::vec4 projectionParams;

		glm::mat4 previousMVPMatrix;

		glm::uvec2 screenSize;
		glm::vec2 padding;

		std::array<glm::mat4, CascadedShadowMapping::CASCADE_COUNT> cascadeMatrices;

		glm::vec4 cascadeSplits;

		std::array<glm::vec4, CascadedShadowMapping::CASCADE_COUNT / 2> cascadeScales;

		glm::uvec4 cascadeTextureSize;

		glm::float_t noiseRotation;
	};
	std::unique_ptr<Wolf::Buffer> m_uniformBuffer;
	std::unique_ptr<Wolf::Sampler> m_shadowMapsSampler;

	// Noise
	static constexpr uint32_t NOISE_TEXTURE_SIZE_PER_SIDE = 128;
	static constexpr uint32_t NOISE_TEXTURE_PATTERN_SIZE_PER_SIDE = 4;
	std::unique_ptr<Wolf::Image> m_noiseImage;
	std::unique_ptr<Wolf::Sampler> m_noiseSampler;
};