#pragma once

#include <glm/glm.hpp>

#include <CommandRecordBase.h>
#include <DescriptorSet.h>
#include <DescriptorSetLayout.h>
#include <DescriptorSetLayoutGenerator.h>
#include <Image.h>
#include <ModelBase.h>
#include <PipelineSet.h>

class PreDepthPass;

class RTGIPass : public Wolf::CommandRecordBase
{
public:
	RTGIPass(PreDepthPass* preDepthPass, std::mutex* vulkanQueueLock) : m_preDepthPass(preDepthPass), m_vulkanQueueLock(vulkanQueueLock) { }

	void addDebugMeshesToRenderList(Wolf::RenderMeshList& renderMeshList) const;
	void updateGraphic() const;

	void initializeResources(const Wolf::InitializationContext& context) override;
	void resize(const Wolf::InitializationContext& context) override;
	void record(const Wolf::RecordContext& context) override;
	void submit(const Wolf::SubmitContext& context) override;

	void initializeDebugPipelineSet();

private:
	PreDepthPass* m_preDepthPass;
	std::mutex* m_vulkanQueueLock;

	inline static const glm::vec3 FIRST_PROBE_POS{ 0.0f, 0.0f, 0.0f};
	inline static const glm::uvec3 PROBE_COUNT{ 30, 30, 30};
	inline static const glm::vec3 SPACE_BETWEEN_PROBES{ 0.5f, 0.5f, 0.5f };

	// Debug
	std::unique_ptr<Wolf::ModelBase> m_sphereModel;

	struct SphereInstanceData
	{
		glm::vec3 worldPos;

		static void getBindingDescription(VkVertexInputBindingDescription& bindingDescription, uint32_t binding)
		{
			bindingDescription.binding = binding;
			bindingDescription.stride = sizeof(SphereInstanceData);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
		}

		static void getAttributeDescriptions(std::vector<VkVertexInputAttributeDescription>& attributeDescriptions, uint32_t binding, uint32_t startLocation)
		{
			const uint32_t attributeDescriptionCountBefore = static_cast<uint32_t>(attributeDescriptions.size());
			attributeDescriptions.resize(attributeDescriptionCountBefore + 1);

			attributeDescriptions[attributeDescriptionCountBefore + 0].binding = binding;
			attributeDescriptions[attributeDescriptionCountBefore + 0].location = startLocation;
			attributeDescriptions[attributeDescriptionCountBefore + 0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[attributeDescriptionCountBefore + 0].offset = offsetof(SphereInstanceData, worldPos);
		}
	};
	std::unique_ptr<Wolf::Buffer> m_sphereInstanceBuffer;
	uint32_t m_sphereInstanceCount = 0;

	std::unique_ptr<Wolf::PipelineSet> m_debugPipelineSet;

	struct DebugUBData
	{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};
	std::unique_ptr<Wolf::Buffer> m_debugUniformBuffer;
	Wolf::DescriptorSetLayoutGenerator m_debugDescriptorSetLayoutGenerator;
	std::unique_ptr<Wolf::DescriptorSetLayout> m_debugDescriptorSetLayout;
	std::unique_ptr<Wolf::DescriptorSet> m_debugDescriptorSet;
};

