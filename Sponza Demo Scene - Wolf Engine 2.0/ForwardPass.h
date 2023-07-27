#pragma once

#include <chrono>
#include <glm/glm.hpp>
#include <vector>

#include <Buffer.h>
#include <CommandRecordBase.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <DescriptorSetLayoutGenerator.h>
#include <FrameBuffer.h>
#include <Mesh.h>
#include <Pipeline.h>
#include <RenderPass.h>
#include <Sampler.h>
#include <ShaderParser.h>

#include "ShadowMaskBasePass.h"

class DepthPass;
class ObjectModel;
class SceneElements;

class ForwardPass : public Wolf::CommandRecordBase
{
public:
	ForwardPass(const SceneElements& sceneElements, DepthPass* preDepthPass, ShadowMaskBasePass* shadowMaskPass);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	void setShadowMaskPass(ShadowMaskBasePass* shadowMaskPass);

private:
	void createPipelines(uint32_t width, uint32_t height);
	void createDescriptorSets();

private:
	const SceneElements& m_sceneElements;

	std::unique_ptr<Wolf::RenderPass> m_renderPass;
	DepthPass* m_preDepthPass;

	std::vector<std::unique_ptr<Wolf::Framebuffer>> m_frameBuffers;
	
	const Wolf::Semaphore* m_preDepthPassSemaphore;
	ShadowMaskBasePass* m_shadowMaskPass;

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
	std::unique_ptr<Wolf::Sampler> m_sampler;

	struct LightUBData
	{
		glm::vec3 directionDirectionalLight;
		float padding;

		glm::vec3 colorDirectionalLight;
	};
	std::unique_ptr<Wolf::Buffer> m_lightUniformBuffer;

	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;
	Wolf::DescriptorSetLayoutGenerator m_descriptorSetLayoutGenerator;
	std::array<std::unique_ptr<Wolf::DescriptorSet>, ShadowMaskBasePass::MASK_COUNT> m_descriptorSets;

	/* UI resources */
	Wolf::DescriptorSetLayoutGenerator m_userInterfaceDescriptorSetLayoutGenerator;
	std::unique_ptr<Wolf::DescriptorSetLayout> m_userInterfaceDescriptorSetLayout;
	std::unique_ptr<Wolf::DescriptorSet> m_userInterfaceDescriptorSet;
	std::unique_ptr<Wolf::Mesh> m_fullscreenRect;
};