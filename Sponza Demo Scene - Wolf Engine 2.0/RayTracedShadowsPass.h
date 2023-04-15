#pragma once

#include <glm/glm.hpp>

#include <CommandRecordBase.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <DescriptorSetLayoutGenerator.h>
#include <Pipeline.h>
#include <ShaderBindingTable.h>
#include <ShaderParser.h>

class SponzaModel;
class DepthPass;

#include "ShadowMaskBasePass.h"

class RayTracedShadowsPass : public Wolf::CommandRecordBase, public ShadowMaskBasePass
{
public:
	RayTracedShadowsPass(const SponzaModel* sponzaModel, DepthPass* preDepthPass);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	Wolf::Image* getOutput(uint32_t frameIdx) override { return m_outputMask.get(); }
	const Wolf::Semaphore* getSemaphore() const override { return Wolf::CommandRecordBase::getSemaphore(); }

private:
	void createPipeline();
	void createDescriptorSet();
	void createOutputImage(uint32_t width, uint32_t height);

private:
	const SponzaModel* m_sponzaModel;
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
		glm::vec4 sunDirection;
	};
	std::unique_ptr<Wolf::Buffer> m_uniformBuffer;
	std::unique_ptr<Wolf::Image> m_outputMask;
};

