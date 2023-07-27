#pragma once

#include "ObjectModel.h"

namespace Wolf
{
	class CameraInterface;
}

class SceneElements
{
public:
	SceneElements();

	void addObject(ObjectModel* object);
	void addImage(Wolf::Image* image);
	void updateUB(const Wolf::CameraInterface& camera) const;
	void drawMeshes(VkCommandBuffer commandBuffer) const;

	const Wolf::Buffer& getMatricesUB() const { return *m_matricesUniformBuffer; }
	uint32_t getImageCount() const { return m_images.size(); }
	VkImageView getImageView(uint32_t imageIdx) const { return m_images[imageIdx]->getDefaultImageView(); }
	const glm::mat4& getTransform(uint32_t modelIdx) const { return m_objects[modelIdx]->getTransform(); }

private:
	std::vector<ObjectModel*> m_objects;
	std::vector<Wolf::Image*> m_images;

	static constexpr uint32_t MAX_MODELS = 2;
	struct MatricesUBData
	{
		glm::mat4 models[MAX_MODELS];
		glm::mat4 view;
		glm::mat4 projection;
	};
	std::unique_ptr<Wolf::Buffer> m_matricesUniformBuffer;
};

