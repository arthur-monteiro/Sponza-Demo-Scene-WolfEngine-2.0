#pragma once

#include <CommandRecordBase.h>
#include <DepthPassBase.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <Image.h>
#include <Pipeline.h>
#include <Sampler.h>
#include <ShaderParser.h>

class SceneElements;

class PreDepthPass : public Wolf::CommandRecordBase, public Wolf::DepthPassBase
{
public:
	PreDepthPass(const SceneElements& sceneElements, bool copyOutput);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	Wolf::Image* getOutput() const override { return m_depthImage.get(); }
	Wolf::Image* getCopy() const { return m_copyImage.get(); }

private:
	void createPipeline();
	void createCopyImage(VkFormat format);

	uint32_t getWidth() override { return m_swapChainWidth; }
	uint32_t getHeight() override { return m_swapChainHeight; }
	VkImageUsageFlags getAdditionalUsages() override { return VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; }
	VkImageLayout getFinalLayout() override { return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; }

	void recordDraws(const Wolf::RecordContext& context) override;
	VkCommandBuffer getCommandBuffer(const Wolf::RecordContext& context) override;

private:
	const SceneElements& m_sceneElements;

	/* Pipeline */
	std::unique_ptr<Wolf::Pipeline> m_pipeline;
	std::unique_ptr<Wolf::ShaderParser> m_vertexShaderParser;
	uint32_t m_swapChainWidth{};
	uint32_t m_swapChainHeight{};

	/* Resources */
	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;
	std::unique_ptr<Wolf::DescriptorSet> m_descriptorSet;
	std::unique_ptr<Wolf::Image> m_copyImage;

	/* Params */
	bool m_copyOutput;
};

