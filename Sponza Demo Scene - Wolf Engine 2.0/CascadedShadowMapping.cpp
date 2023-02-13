#include "CascadedShadowMapping.h"

CascadeDepthPass::CascadeDepthPass(uint32_t width, uint32_t height) : m_width(width), m_height(height)
{
}

void CascadeDepthPass::recordDraws(const Wolf::RecordContext& context)
{
}

VkCommandBuffer CascadeDepthPass::getCommandBuffer(const Wolf::RecordContext& context)
{
	return VkCommandBuffer();
}

CascadedShadowMapping::CascadedShadowMapping(const Wolf::Mesh* sponzaMesh)
{
	m_sponzaMesh = sponzaMesh;
}

void CascadedShadowMapping::initializeResources(const Wolf::InitializationContext& context)
{
}

void CascadedShadowMapping::resize(const Wolf::InitializationContext& context)
{
}

void CascadedShadowMapping::record(const Wolf::RecordContext& context)
{
}

void CascadedShadowMapping::submit(const Wolf::SubmitContext& context)
{
}

void CascadedShadowMapping::createPipeline(uint32_t width, uint32_t height)
{
}
