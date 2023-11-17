#pragma once

#include <glm/glm.hpp>

struct GameContext
{
	glm::vec3 sunDirection;
	float sunPhi, sunTheta;
	float sunAreaAngle;
	glm::vec3 sunColor;
	bool enableTAA;

	bool shadowmapScreenshotsRequested;

	GameContext(const glm::vec3& defaultSunDirection, const glm::vec3& defaultSunColor)
	{
		sunDirection = defaultSunDirection;
		sunColor = defaultSunColor;
	}
};