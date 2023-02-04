#pragma once

#include <WolfEngine.h>

#include "LoadingScreenUniquePass.h"
#include "SponzaModel.h"
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

	static void debugCallback(Wolf::Debug::Severity severity, Wolf::Debug::Type type, std::string message);
	ultralight::JSValue getFrameRate(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args);

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
};

