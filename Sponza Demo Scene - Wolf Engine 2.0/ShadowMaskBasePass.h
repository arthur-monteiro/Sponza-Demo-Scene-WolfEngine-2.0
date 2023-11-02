#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace Wolf
{
	class Image;
	class Semaphore;
}

class ShadowMaskBasePass
{
public:
	static constexpr uint32_t MASK_COUNT = 2;

	virtual Wolf::Image* getOutput(uint32_t frameIdx) = 0;
	virtual const Wolf::Semaphore* getSemaphore() const = 0;
	virtual void getConditionalBlocksToEnableWhenReadingMask(std::vector<std::string>& conditionalBlocks) const = 0;
	virtual Wolf::Image* getDenoisingPatternImage() = 0;
	virtual Wolf::Image* getDebugImage() const { return nullptr; }
};

