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

	Wolf::ResourceUniqueOwner<LoadingScreenUniquePass> m_loadingScreenUniquePass;
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
};

