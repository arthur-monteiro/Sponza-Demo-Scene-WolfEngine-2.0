#pragma once

#include <glm/glm.hpp>

#include <CommandRecordBase.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <DescriptorSetLayoutGenerator.h>
#include <Pipeline.h>
#include <ShaderBindingTable.h>
#include <ShaderParser.h>

class DepthPass;
class ObjectModel;
class SharedGPUResources;

#include "Sampler.h"
#include "ShadowMaskBasePass.h"

class RayTracedShadowsPass : public Wolf::CommandRecordBase, public ShadowMaskBasePass
{
public:
	RayTracedShadowsPass(const ObjectModel* sponzaModel, DepthPass* preDepthPass);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	Wolf::Image* getOutput(uint32_t frameIdx) override { return m_outputMask.get(); }
	const Wolf::Semaphore* getSemaphore() const override { return Wolf::CommandRecordBase::getSemaphore(); }

	void saveMaskToFile(const std::string& filename);

private:
	void createPipeline();
	void createDescriptorSet();
	void createOutputImage(uint32_t width, uint32_t height);

	static float jitter();

private:
	const ObjectModel* m_sponzaModel;
	DepthPass* m_preDepthPass;

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
		glm::mat4 invModelView;
		glm::mat4 invProjection;
		glm::vec4 projectionParams;
		glm::vec4 sunDirectionAndNoiseIndex;

		glm::uint drawWithoutNoiseFrameIndex; // 0 = draw with noise
		glm::vec3 padding;
	};
	std::unique_ptr<Wolf::Buffer> m_uniformBuffer;
	std::unique_ptr<Wolf::Image> m_outputMask;

	// Noise
	static constexpr uint32_t NOISE_TEXTURE_SIZE_PER_SIDE = 128;
	static constexpr uint32_t NOISE_TEXTURE_VECTOR_COUNT = 16;
	std::unique_ptr<Wolf::Image> m_noiseImage;
	std::unique_ptr<Wolf::Sampler> m_noiseSampler;
};

