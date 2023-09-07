#include "SceneElements.h"

#include <CameraInterface.h>

using namespace Wolf;

SceneElements::SceneElements()
{
	m_matricesUniformBuffer.reset(new Buffer(sizeof(MatricesUBData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, UpdateRate::NEVER));
}

void SceneElements::addObject(ObjectModel* object)
{
	m_objects.push_back(object);
	object->getImages(m_images);
}

void SceneElements::addImage(Wolf::Image* image)
{
	m_images.push_back(image);
}

void SceneElements::updateUB(const Wolf::CameraInterface& camera) const
{
	MatricesUBData mvp;
	mvp.projection = camera.getProjectionMatrix();
	mvp.view = camera.getViewMatrix();
	for (uint32_t i = 0; i < m_objects.size(); ++i)
	{
		mvp.models[i] = m_objects[i]->getTransform();
	}
	m_matricesUniformBuffer->transferCPUMemory(&mvp, sizeof(mvp), 0 /* srcOffet */);
}

void SceneElements::drawMeshes(VkCommandBuffer commandBuffer) const
{
	for (const auto& object : m_objects)
	{
		object->getMesh()->draw(commandBuffer);
	}
}
