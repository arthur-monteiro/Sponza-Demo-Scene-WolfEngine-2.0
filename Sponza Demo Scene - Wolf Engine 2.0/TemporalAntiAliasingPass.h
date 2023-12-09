#pragma once

#include <glm/glm.hpp>

#include <CommandRecordBase.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <DescriptorSetLayoutGenerator.h>
#include <Pipeline.h>
#include <ResourceUniqueOwner.h>
#include <ShaderParser.h>

class PreDepthPass;
class ForwardPass;
class ShadowMaskBasePass;

class TemporalAntiAliasingPass : public Wolf::CommandRecordBase
{
public:
	TemporalAntiAliasingPass(const Wolf::ResourceNonOwner<PreDepthPass>& preDepthPass, const Wolf::ResourceNonOwner<ForwardPass>& forwardPass) : m_preDepthPass(preDepthPass), m_forwardPass(forwardPass) {}

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

private:
	void createPipeline();
	void updateDescriptorSets() const;

	Wolf::ResourceNonOwner<PreDepthPass> m_preDepthPass;
	Wolf::ResourceNonOwner<ForwardPass> m_forwardPass;

	std::unique_ptr<Wolf::ShaderParser> m_computeShaderParser;
	std::unique_ptr<Wolf::Pipeline> m_pipeline;

	Wolf::DescriptorSetLayoutGenerator m_descriptorSetLayoutGenerator;
	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;
	std::array<std::unique_ptr<Wolf::DescriptorSet>, 2> m_descriptorSets;

	struct ReprojectionUBData
	{
		glm::mat4 invView;

		glm::mat4 invProjection;

		glm::vec4 projectionParams;

		glm::mat4 previousMVPMatrix;

		glm::uvec2 screenSize;
		uint32_t enableTAA;
		uint32_t padding;
	};
	std::unique_ptr<Wolf::Buffer> m_uniformBuffer;
};

