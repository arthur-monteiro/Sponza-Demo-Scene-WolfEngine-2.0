#pragma once

#include <CommandBuffer.h>
#include <CommandRecordBase.h>
#include <DepthPassBase.h>
#include <Image.h>
#include <ShaderParser.h>

#include "CameraList.h"
#include "OrthographicCamera.h"

class SceneElements;
class PreDepthPass;

class CascadeDepthPass : public Wolf::DepthPassBase
{
public:
	CascadeDepthPass(const Wolf::InitializationContext& context, uint32_t width, uint32_t height, const Wolf::CommandBuffer* commandBuffer, uint32_t cameraIdx);
	CascadeDepthPass(const CascadeDepthPass&) = delete;

	void getViewProjMatrix(glm::mat4& output) const { output = m_camera->getProjectionMatrix() * m_camera->getViewMatrix(); }
	void setCameraInfos(const glm::vec3& center, float radius, const glm::vec3& direction) const;

	void addCameraForThisFrame(Wolf::CameraList& cameraList) const { cameraList.addCameraForThisFrame(m_camera.get(), m_cameraIdx); }

private:
	uint32_t getWidth() override { return m_width; }
	uint32_t getHeight() override { return m_height; }

	void recordDraws(const Wolf::RecordContext& context) override;
	VkCommandBuffer getCommandBuffer(const Wolf::RecordContext& context) override;
	VkImageUsageFlags getAdditionalUsages() override { return VK_IMAGE_USAGE_SAMPLED_BIT; }
	VkImageLayout getFinalLayout() override { return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }

	/* Shared resources */
	const Wolf::CommandBuffer* m_commandBuffer;

	/* Owned resources */
	std::unique_ptr<Wolf::OrthographicCamera> m_camera;
	uint32_t m_cameraIdx;
	uint32_t m_width, m_height;
};

class CascadedShadowMapping : public Wolf::CommandRecordBase
{
public:
	static constexpr int CASCADE_COUNT = 4;

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	Wolf::Image* getShadowMap(uint32_t cascadeIdx) const { return m_cascadeDepthPasses[cascadeIdx]->getOutput(); }
	float getCascadeSplit(uint32_t cascadeIdx) const { return m_cascadeSplits[cascadeIdx]; }
	void getCascadeMatrix(uint32_t cascadeIdx, glm::mat4& output) const { m_cascadeDepthPasses[cascadeIdx]->getViewProjMatrix(output); }
	uint32_t getCascadeTextureSize(uint32_t cascadeIdx) const { return m_cascadeTextureSize[cascadeIdx]; }

	void addCamerasForThisFrame(Wolf::CameraList& cameraList) const;

private:
	/* Cascades */
	uint32_t m_cascadeTextureSize[CASCADE_COUNT] = { 3072, 3072, 3072, 3072 };
	std::array<std::unique_ptr<CascadeDepthPass>, CASCADE_COUNT> m_cascadeDepthPasses;
	std::array<float, CASCADE_COUNT> m_cascadeSplits{};
};