#pragma once

#include <BottomLevelAccelerationStructure.h>
#include <DescriptorSetLayout.h>
#include <DescriptorSetLayoutGenerator.h>
#include <Image.h>
#include <LazyInitSharedResource.h>
#include <ObjLoader.h>
#include <PipelineSet.h>
#include <TopLevelAccelerationStructure.h>

struct GameContext;

namespace Wolf
{
	class CameraInterface;
	class RenderMeshList;
	class BindlessDescriptor;
}

class ObjectModel
{
public:
	ObjectModel(std::mutex* vulkanQueueLock, const glm::mat4& transform, bool buildAccelerationStructures, const std::string& filename, const std::string& mtlFolder, bool loadMaterials, uint32_t materialIdOffset,
		const Wolf::BindlessDescriptor* bindlessDescriptor);

	void addMeshesToRenderList(Wolf::RenderMeshList& renderMeshList) const;
	void updateGraphic(const Wolf::CameraInterface& camera, const GameContext& gameContext) const;

	const Wolf::Mesh* getMesh() const { return m_modelData.mesh.get(); }
	void getImages(std::vector<Wolf::Image*>& images) const { m_modelData.getImages(images); }
	const glm::mat4& getTransform() const { return m_transform; }
	const Wolf::TopLevelAccelerationStructure& getTLAS() const { return *m_tlas; }
	VkDescriptorSetLayout getDescriptorSetLayout() const { return m_modelDescriptorSetLayout->getResource()->getDescriptorSetLayout(); }

	void setPosition(glm::vec3 position) { m_transform[3] = glm::vec4(position, 1.0f); }
	void setPipelineSet(Wolf::PipelineSet* pipelineSet) { m_pipelineSet = pipelineSet; }

private:
	void buildAccelerationStructures();

private:
	Wolf::PipelineSet* m_pipelineSet = nullptr;

	std::unique_ptr<Wolf::LazyInitSharedResource<Wolf::DescriptorSetLayoutGenerator, ObjectModel>> m_modelDescriptorSetLayoutGenerator;
	std::unique_ptr<Wolf::LazyInitSharedResource<Wolf::DescriptorSetLayout, ObjectModel>> m_modelDescriptorSetLayout;

	Wolf::ModelData m_modelData;
	glm::mat4 m_transform;

	struct MatricesUBData
	{
		glm::mat4 model;
	};

	std::unique_ptr<Wolf::DescriptorSet> m_descriptorSet;
	std::unique_ptr<Wolf::Buffer> m_matricesUniformBuffer;

	std::unique_ptr<Wolf::BottomLevelAccelerationStructure> m_blas;
	std::unique_ptr<Wolf::TopLevelAccelerationStructure> m_tlas;
};
