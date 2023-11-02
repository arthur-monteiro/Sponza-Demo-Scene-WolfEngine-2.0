#pragma once

#define _USE_MATH_DEFINES
#include <math.h>
#include <WolfEngine.h>

#include "GameContext.h"
#include "LoadingScreenUniquePass.h"
#include "SponzaScene.h"

enum class GAME_STATE
{
	LOADING,
	RUNNING
};

class SystemManager
{
public:
	SystemManager();
	void run();

private:
	void createWolfInstance();
	void loadSponzaScene();

	static void debugCallback(Wolf::Debug::Severity severity, Wolf::Debug::Type type, const std::string& message);
	void bindUltralightCallbacks();
	ultralight::JSValue getFrameRate(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);
	void setSunTheta(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);
	void setSunPhi(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);
	void setShadows(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);
	void setSunAreaAngle(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);
	void setDebugMode(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);
	void setEnableTAA(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);

private:
	std::unique_ptr<Wolf::WolfEngine> m_wolfInstance;

	std::unique_ptr<LoadingScreenUniquePass> m_loadingScreenUniquePass;
	std::unique_ptr<SponzaScene> m_sponzaScene;

	GAME_STATE m_gameState = GAME_STATE::LOADING;
	std::thread m_sceneLoadingThread;
	std::mutex m_mutex;
	bool m_needJoinLoadingThread = false;

	uint32_t m_currentFramesAccumulated = 0;
	uint32_t m_stableFPS = 0;
	std::chrono::steady_clock::time_point m_startTimeFPSCounter = std::chrono::steady_clock::now();

	std::vector<GameContext> m_gameContexts;

	double m_sunTheta = 0.0, m_sunPhi = 0.0;
	double m_sunAreaAngle = 0.01;
	bool m_TAAEnabled = true;
	const glm::vec2 JITTER_OFFSET[16] = {
		glm::vec2(0.500000f, 0.333333f),
		glm::vec2(0.250000f, 0.666667),
		glm::vec2(0.750000f, 0.111111f),
		glm::vec2(0.125000f, 0.444444f),
		glm::vec2(0.625000f, 0.777778f),
		glm::vec2(0.375000f, 0.222222f),
		glm::vec2(0.875000f, 0.555556f),
		glm::vec2(0.062500f, 0.888889f),
		glm::vec2(0.562500f, 0.037037f),
		glm::vec2(0.312500f, 0.370370f),
		glm::vec2(0.812500f, 0.703704f),
		glm::vec2(0.187500f, 0.148148f),
		glm::vec2(0.687500f, 0.481481f),
		glm::vec2(0.437500f, 0.814815f),
		glm::vec2(0.937500f, 0.259259f),
		glm::vec2(0.031250f, 0.592593)
	};
};

