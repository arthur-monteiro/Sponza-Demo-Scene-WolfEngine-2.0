#pragma once

#include <glm/glm.hpp>

#include <Buffer.h>
#include <CommandBuffer.h>
#include <CommandRecordBase.h>
#include <DepthPassBase.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <DescriptorSetLayoutGenerator.h>
#include <FrameBuffer.h>
#include <Image.h>
#include <Mesh.h>
#include <Pipeline.h>
#include <RenderPass.h>
#include <Sampler.h>
#include <ShaderParser.h>

class DepthPass;

class CascadeDepthPass : public Wolf::DepthPassBase
{
public:
	CascadeDepthPass(const Wolf::InitializationContext& context, const Wolf::Mesh* sponzaMesh, uint32_t width, uint32_t height, const Wolf::CommandBuffer* commandBuffer, VkDescriptorSetLayout descriptorSetLayout,
		const Wolf::ShaderParser* vertexShaderParser, const Wolf::DescriptorSetLayoutGenerator& descriptorSetLayoutGenerator);
	CascadeDepthPass(const CascadeDepthPass&) = delete;

	void getMVP(glm::mat4& output) const { output = m_mvpData.mvp; }
	void setMVP(const glm::mat4& mvp);
	void shaderChanged();

private:
	uint32_t getWidth() override { return m_width; }
	uint32_t getHeight() override { return m_height; }

	void recordDraws(const Wolf::RecordContext& context) override;
	VkCommandBuffer getCommandBuffer(const Wolf::RecordContext& context) override;
	VkImageUsageFlags getAdditionalUsages() override { return VK_IMAGE_USAGE_SAMPLED_BIT; }
	VkImageLayout getFinalLayout() override { return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }

private:
	void createPipeline();

	/* Shared resources */
	VkDescriptorSetLayout m_descriptorSetLayout;
	const Wolf::ShaderParser* m_vertexShaderParser;
	const Wolf::CommandBuffer* m_commandBuffer;
	const Wolf::Mesh* m_sponzaMesh;

	/* Owned resources */
	std::unique_ptr<Wolf::Pipeline> m_pipeline;
	uint32_t m_width, m_height;
	struct UBData
	{
		glm::mat4 mvp;
	};
	UBData m_mvpData;
	std::unique_ptr<Wolf::Buffer> m_uniformBuffer;
	std::unique_ptr<Wolf::DescriptorSet> m_descriptorSet;
};

class CascadedShadowMapping : public Wolf::CommandRecordBase
{
public:
	static constexpr int CASCADE_COUNT = 4;

	CascadedShadowMapping(const Wolf::Mesh* sponzaMesh);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	Wolf::Image* getShadowMap(uint32_t cascadeIdx) const { return m_cascadeDepthPasses[cascadeIdx]->getOutput(); }
	float getCascadeSplit(uint32_t cascadeIdx) const { return m_cascadeSplits[cascadeIdx]; }
	void getCascadeMatrix(uint32_t cascadeIdx, glm::mat4& output) const { m_cascadeDepthPasses[cascadeIdx]->getMVP(output); }
	uint32_t getCascadeTextureSize(uint32_t cascadeIdx) const { return m_cascadeTextureSize[cascadeIdx]; }

private:
	/* Pipeline */
	std::unique_ptr<Wolf::ShaderParser> m_vertexShaderParser;
	std::unique_ptr<Wolf::Pipeline> m_pipeline;

	/* Resources*/
	const Wolf::Mesh* m_sponzaMesh;
	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;

	/* Cascades */
	uint32_t m_cascadeTextureSize[CASCADE_COUNT] = { 2048, 1024, 1024, 512 };
	std::array<std::unique_ptr<CascadeDepthPass>, CASCADE_COUNT> m_cascadeDepthPasses;
	std::array<float, CASCADE_COUNT> m_cascadeSplits;
};