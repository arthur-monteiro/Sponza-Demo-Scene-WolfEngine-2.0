#pragma once

#include <glm/glm.hpp>

#include <Buffer.h>
#include <CommandBuffer.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <DescriptorSetLayoutGenerator.h>
#include <FrameBuffer.h>
#include <Image.h>
#include <Mesh.h>
#include <PassBase.h>
#include <Pipeline.h>
#include <RenderPass.h>
#include <Sampler.h>
#include <ShaderParser.h>

class DepthPass : public Wolf::PassBase
{
public:
	DepthPass(const Wolf::Mesh* sponzaMesh);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	const Wolf::Semaphore* getSemaphore() const { return m_semaphore.get(); }

private:
	void createDepthImage(const Wolf::InitializationContext& context);
	void createPipeline(uint32_t width, uint32_t height);

private:
	std::unique_ptr<Wolf::RenderPass> m_renderPass;
	std::unique_ptr<Wolf::Image> m_depthImage;

	std::unique_ptr<Wolf::CommandBuffer> m_commandBuffer;
	std::vector<std::unique_ptr<Wolf::Framebuffer>> m_frameBuffers;

	std::unique_ptr<Wolf::Semaphore> m_semaphore;

	/* Pipeline */
	std::unique_ptr<Wolf::ShaderParser> m_vertexShaderParser;

	std::unique_ptr<Wolf::Pipeline> m_pipeline;
	uint32_t m_swapChainWidth;
	uint32_t m_swapChainHeight;

	/* Resources*/
	const Wolf::Mesh* m_sponzaMesh;
	struct UBData
	{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};
	std::unique_ptr<Wolf::Buffer> m_uniformBuffer;

	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;
	std::unique_ptr<Wolf::DescriptorSet> m_descriptorSet;
};

