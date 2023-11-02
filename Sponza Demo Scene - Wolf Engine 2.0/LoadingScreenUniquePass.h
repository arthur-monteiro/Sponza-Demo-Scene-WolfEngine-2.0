#pragma once

#include <Buffer.h>
#include <CommandBuffer.h>
#include <CommandRecordBase.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <FrameBuffer.h>
#include <Image.h>
#include <Mesh.h>
#include <Pipeline.h>
#include <RenderPass.h>
#include <Sampler.h>
#include <ShaderParser.h>

class LoadingScreenUniquePass : public Wolf::CommandRecordBase
{
public:
	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	const Wolf::Semaphore* getSemaphore() const override { return m_semaphore.get(); }

private:
	void createPipelines(uint32_t width, uint32_t height);

private:
	/* Pass params */
	std::unique_ptr<Wolf::RenderPass> m_renderPass;
	std::vector<std::unique_ptr<Wolf::Framebuffer>> m_frameBuffers;
	std::unique_ptr<Wolf::Semaphore> m_semaphore;

	/* Pipeline */
	std::unique_ptr<Wolf::ShaderParser> m_loadingScreenVertexShaderParser;
	std::unique_ptr<Wolf::ShaderParser> m_loadingScreenFragmentShaderParser;
	std::unique_ptr<Wolf::Pipeline> m_loadingScreenPipeline;
	std::unique_ptr<Wolf::ShaderParser> m_loadingIconVertexShaderParser;
	std::unique_ptr<Wolf::ShaderParser> m_loadingIconFragmentShaderParser;
	std::unique_ptr<Wolf::Pipeline> m_loadingIconPipeline;
	uint32_t m_swapChainWidth;
	uint32_t m_swapChainHeight;

	/* Resources */
	std::unique_ptr<Wolf::Mesh> m_rectMesh;
	std::unique_ptr<Wolf::Image> m_loadingScreenTexture;
	std::unique_ptr<Wolf::Image> m_loadingIconTexture;
	std::unique_ptr<Wolf::Sampler> m_sampler;
	std::chrono::steady_clock::time_point m_startTimer = std::chrono::steady_clock::now();
	std::unique_ptr<Wolf::Buffer> m_loadingIconUniformBuffer;
	std::unique_ptr<Wolf::DescriptorSetLayout> m_loadingScreenDescriptorSetLayout;
	std::unique_ptr<Wolf::DescriptorSet> m_loadingScreenDescriptorSet;
	std::unique_ptr<Wolf::DescriptorSetLayout> m_loadingIconDescriptorSetLayout;
	std::unique_ptr<Wolf::DescriptorSet> m_loadingIconDescriptorSet;
};

