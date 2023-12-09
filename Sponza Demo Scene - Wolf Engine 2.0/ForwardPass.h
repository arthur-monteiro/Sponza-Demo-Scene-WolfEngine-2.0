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
#include <ResourceUniqueOwner.h>
#include <Sampler.h>
#include <ShaderParser.h>

#include "ShadowMaskBasePass.h"

class PreDepthPass;
class RTGIPass;
class SceneElements;

class ForwardPass : public Wolf::CommandRecordBase
{
public:
	ForwardPass(const Wolf::ResourceNonOwner<PreDepthPass>& preDepthPass, const Wolf::ResourceNonOwner<ShadowMaskBasePass>& shadowMaskPass, const Wolf::ResourceNonOwner<RTGIPass>& rayTracedGIPass);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	uint32_t getOutputImageCount() const { return static_cast<uint32_t>(m_outputImages.size()); }
	Wolf::ResourceNonOwner<Wolf::Image> getOutputImage(uint32_t idx) { return m_outputImages[idx].createNonOwnerResource(); }
	Wolf::ResourceNonOwner<Wolf::Image> getVelocityImage() { return m_velocityImage.createNonOwnerResource(); }
	
	void setShadowMaskPass(const Wolf::ResourceNonOwner<ShadowMaskBasePass>& shadowMaskPass);

	enum class DebugMode { None, Shadows, RTGI };
	void setDebugMode(DebugMode debugMode);

private:
	void createOutputImages(uint32_t width, uint32_t height);
	void createUIPipeline(uint32_t width, uint32_t height);
	void createDescriptorSetLayout();
	void createDescriptorSets(bool forceReset);
	void createOrUpdateDebugDescriptorSet();

private:
	std::unique_ptr<Wolf::RenderPass> m_renderPass;
	Wolf::ResourceNonOwner<PreDepthPass> m_preDepthPass;
	
	std::array<Wolf::ResourceUniqueOwner<Wolf::Image>, 2> m_outputImages;
	Wolf::ResourceUniqueOwner<Wolf::Image> m_velocityImage;
	std::vector<std::unique_ptr<Wolf::Framebuffer>> m_frameBuffers;
	
	const Wolf::Semaphore* m_preDepthPassSemaphore;
	Wolf::ResourceNonOwner<ShadowMaskBasePass> m_shadowMaskPass;
	Wolf::ResourceNonOwner<RTGIPass> m_rayTracedGIPass;

	/* Pipeline */
	std::unique_ptr<Wolf::ShaderParser> m_userInterfaceVertexShaderParser;
	std::unique_ptr<Wolf::ShaderParser> m_userInterfaceFragmentShaderParser;
	
	std::unique_ptr<Wolf::Pipeline> m_drawFullScreenImagePipeline;
	uint32_t m_swapChainWidth;
	uint32_t m_swapChainHeight;

	/* Resources*/
	std::unique_ptr<Wolf::Sampler> m_sampler;

	struct LightUBData
	{
		glm::vec3 directionDirectionalLight;
		float padding;

		glm::vec3 colorDirectionalLight;
		float padding2;

		glm::uvec2 outputSize;
	};
	std::unique_ptr<Wolf::Buffer> m_lightUniformBuffer;

	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;
	Wolf::DescriptorSetLayoutGenerator m_descriptorSetLayoutGenerator;
	std::array<std::unique_ptr<Wolf::DescriptorSet>, ShadowMaskBasePass::MASK_COUNT> m_descriptorSets;

	/* UI and debug resources */
	Wolf::DescriptorSetLayoutGenerator m_drawFullScreenImageDescriptorSetLayoutGenerator;
	std::unique_ptr<Wolf::DescriptorSetLayout> m_drawFullScreenImageDescriptorSetLayout;
	std::unique_ptr<Wolf::DescriptorSet> m_userInterfaceDescriptorSet;
	std::unique_ptr<Wolf::DescriptorSet> m_debugDescriptorSet;
	std::unique_ptr<Wolf::Mesh> m_fullscreenRect;
	Wolf::Image* m_usedDebugImage;
};