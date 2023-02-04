#pragma once

#include <Buffer.h>
#include <CommandBuffer.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <FrameBuffer.h>
#include <Image.h>
#include <Mesh.h>
#include <PassBase.h>
#include <Pipeline.h>
#include <RenderPass.h>
#include <Sampler.h>
#include <ShaderParser.h>

class LoadingScreenUniquePass : public Wolf::PassBase
{
public:
	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	const Wolf::Semaphore* getSemaphore() const { return m_semaphore.get(); }

private:
	void createPipeline(uint32_t width, uint32_t height);

private:
	/* Pass params */
	std::unique_ptr<Wolf::RenderPass> m_renderPass;
	std::unique_ptr<Wolf::CommandBuffer> m_commandBuffer;
	std::vector<std::unique_ptr<Wolf::Framebuffer>> m_frameBuffers;
	std::unique_ptr<Wolf::Semaphore> m_semaphore;

	/* Pipeline */
	std::unique_ptr<Wolf::ShaderParser> m_vertexShaderParser;
	std::unique_ptr<Wolf::ShaderParser> m_fragmentShaderParser;
	std::unique_ptr<Wolf::Pipeline> m_pipeline;
	uint32_t m_swapChainWidth;
	uint32_t m_swapChainHeight;

	/* Resources */
	std::unique_ptr<Wolf::Mesh> m_fullscreenRect;
	std::unique_ptr<Wolf::Image> m_loadingScreenTexture;
	std::unique_ptr<Wolf::Sampler> m_sampler;
	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;
	std::unique_ptr<Wolf::DescriptorSet> m_descriptorSet;
};

