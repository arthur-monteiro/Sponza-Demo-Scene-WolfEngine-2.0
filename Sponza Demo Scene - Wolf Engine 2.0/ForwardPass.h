#pragma once

#include <chrono>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>

#include <Buffer.h>
#include <CommandBuffer.h>
#include <CommandRecordBase.h>
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

class ForwardPass : public Wolf::CommandRecordBase
{
public:
	ForwardPass(const Wolf::Mesh* sponzaMesh, std::vector<Wolf::Image*> images, const Wolf::Semaphore* waitSemaphore, DepthPass* preDepthPass);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	const Wolf::Semaphore* getSemaphore() const { return m_signalSemaphore.get(); }

private:
	void createPipelines(uint32_t width, uint32_t height);

private:
	std::unique_ptr<Wolf::RenderPass> m_renderPass;
	DepthPass* m_preDepthPass;

	std::vector<std::unique_ptr<Wolf::Framebuffer>> m_frameBuffers;

	std::unique_ptr<Wolf::Semaphore> m_signalSemaphore;
	const Wolf::Semaphore* m_waitSemaphore;

	/* Pipeline */
	std::unique_ptr<Wolf::ShaderParser> m_vertexShaderParser;
	std::unique_ptr<Wolf::ShaderParser> m_fragmentShaderParser;

	std::unique_ptr<Wolf::ShaderParser> m_userInterfaceVertexShaderParser;
	std::unique_ptr<Wolf::ShaderParser> m_userInterfaceFragmentShaderParser;

	std::unique_ptr<Wolf::Pipeline> m_pipeline;
	std::unique_ptr<Wolf::Pipeline> m_userInterfacePipeline;
	uint32_t m_swapChainWidth;
	uint32_t m_swapChainHeight;

	/* Resources*/
	const Wolf::Mesh* m_sponzaMesh;
	std::vector<Wolf::Image*> m_sponzaImages;
	std::unique_ptr<Wolf::Sampler> m_sampler;
	struct UBData
	{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};
	std::unique_ptr<Wolf::Buffer> m_uniformBuffer;

	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;
	std::unique_ptr<Wolf::DescriptorSet> m_descriptorSet;

	/* UI resources */
	Wolf::DescriptorSetLayoutGenerator m_userInterfaceDescriptorSetLayoutGenerator;
	std::unique_ptr<Wolf::DescriptorSetLayout> m_userInterfaceDescriptorSetLayout;
	std::unique_ptr<Wolf::DescriptorSet> m_userInterfaceDescriptorSet;
	std::unique_ptr<Wolf::Mesh> m_fullscreenRect;
};