#pragma once

#include <vulkan/vulkan_core.h>

namespace CommonDescriptorLayouts
{
	extern VkDescriptorSetLayout g_commonForwardDescriptorSetLayout;
}

namespace CommonCameraIndices
{
	constexpr uint32_t CAMERA_IDX_ACTIVE			= 0;
	constexpr uint32_t CAMERA_IDX_SHADOW_CASCADE_0  = 1; // make sure to keep cascade idx in the same order
	constexpr uint32_t CAMERA_IDX_SHADOW_CASCADE_1  = 2;
	constexpr uint32_t CAMERA_IDX_SHADOW_CASCADE_2  = 3;
	constexpr uint32_t CAMERA_IDX_SHADOW_CASCADE_3  = 4;
}

namespace CommonPipelineIndices
{
	constexpr uint32_t PIPELINE_IDX_PRE_DEPTH  = 0;
	constexpr uint32_t PIPELINE_IDX_SHADOW_MAP = 1;
	constexpr uint32_t PIPELINE_IDX_FORWARD    = 2;
}
