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

class SceneElements;

class DepthPass : public Wolf::CommandRecordBase, public Wolf::DepthPassBase
{
public:
	DepthPass(const SceneElements& sceneElements);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	Wolf::Image* getOutput() const { return m_depthImage.get(); }

private:
	void createPipeline();

	uint32_t getWidth() override { return m_swapChainWidth; }
	uint32_t getHeight() override { return m_swapChainHeight; }
	VkImageUsageFlags getAdditionalUsages() override { return VK_IMAGE_USAGE_SAMPLED_BIT; }
	VkImageLayout getFinalLayout() override { return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }

	void recordDraws(const Wolf::RecordContext& context) override;
	VkCommandBuffer getCommandBuffer(const Wolf::RecordContext& context) override;

private:
	const SceneElements& m_sceneElements;

	/* Pipeline */
	std::unique_ptr<Wolf::Pipeline> m_pipeline;
	std::unique_ptr<Wolf::ShaderParser> m_vertexShaderParser;
	uint32_t m_swapChainWidth{};
	uint32_t m_swapChainHeight{};

	/* Resources*/
	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;
	std::unique_ptr<Wolf::DescriptorSet> m_descriptorSet;
};

