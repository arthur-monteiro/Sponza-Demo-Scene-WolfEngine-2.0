#pragma once

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

class CascadeDepthPass : public Wolf::DepthPassBase
{
public:
	CascadeDepthPass(uint32_t width, uint32_t height);

private:
	uint32_t getWidth() override { return m_width; }
	uint32_t getHeight() override { return m_height; }

	void recordDraws(const Wolf::RecordContext& context) override;
	VkCommandBuffer getCommandBuffer(const Wolf::RecordContext& context) override;

private:
	uint32_t m_width, m_height;
};

class CascadedShadowMapping : public Wolf::CommandRecordBase
{
public:
	CascadedShadowMapping(const Wolf::Mesh* sponzaMesh);

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

private:
	void createPipeline(uint32_t width, uint32_t height);

private:
	/* Pipeline */
	std::unique_ptr<Wolf::ShaderParser> m_vertexShaderParser;
	std::unique_ptr<Wolf::Pipeline> m_pipeline;

	/* Resources*/
	const Wolf::Mesh* m_sponzaMesh;
	std::unique_ptr<Wolf::DescriptorSetLayout> m_descriptorSetLayout;
};

