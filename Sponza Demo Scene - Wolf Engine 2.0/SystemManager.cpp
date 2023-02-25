#include "SystemManager.h"

#include <TextFileReader.h>

using namespace Wolf;

SystemManager::SystemManager()
{
	createWolfInstance();

	m_loadingScreenUniquePass.reset(new LoadingScreenUniquePass());
	m_wolfInstance->initializePass(m_loadingScreenUniquePass.get());

	m_sceneLoadingThread = std::thread(&SystemManager::loadSponzaScene, this);
}

void SystemManager::run()
{
	while (!m_wolfInstance->windowShouldClose() /* check if the window should close (for example if the user pressed alt+f4)*/)
	{
		if (m_needJoinLoadingThread)
		{
			m_sceneLoadingThread.join();
			m_needJoinLoadingThread = false;
		}

		if (m_gameState == GAME_STATE::LOADING)
		{
			m_mutex.lock();
			std::vector<Wolf::CommandRecordBase*> passes(1);
			passes[0] = m_loadingScreenUniquePass.get();

			m_wolfInstance->frame(passes, m_loadingScreenUniquePass->getSemaphore());

			m_mutex.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		else if (m_gameState == GAME_STATE::RUNNING)
		{
			m_sponzaScene->update();
			m_sponzaScene->frame(m_wolfInstance.get());
		}

		const auto currentTime = std::chrono::steady_clock::now();
		m_currentFramesAccumulated++;
		const long long durationInMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - m_startTimeFPSCounter).count();
		if (durationInMs > 1000)
		{
			m_stableFPS = std::round((1000.0f * m_currentFramesAccumulated) / static_cast<float>(durationInMs));

			m_currentFramesAccumulated = 0;
			m_startTimeFPSCounter = currentTime;
		}
	}

	m_wolfInstance->waitIdle();
}

void SystemManager::createWolfInstance()
{
	WolfInstanceCreateInfo wolfInstanceCreateInfo;
	wolfInstanceCreateInfo.configFilename = "config/config.ini";
	wolfInstanceCreateInfo.debugCallback = debugCallback;

	TextFileReader UIHtmlReader("UI/UI.html");
	wolfInstanceCreateInfo.htmlStringUI = UIHtmlReader.getFileContent().c_str();

	m_wolfInstance.reset(new WolfEngine(wolfInstanceCreateInfo));

	ultralight::JSObject jsObject;
	m_wolfInstance->getUserInterfaceJSObject(jsObject);
	jsObject["getFrameRate"] = (ultralight::JSCallbackWithRetval)std::bind(&SystemManager::getFrameRate, this, std::placeholders::_1, std::placeholders::_2);

	m_gameContexts.reserve(g_configuration->getMaxCachedFrames());
	std::vector<void*> contextPtrs(g_configuration->getMaxCachedFrames());
	for (uint32_t i = 0; i < g_configuration->getMaxCachedFrames(); ++i)
	{
		m_gameContexts.push_back(GameContext(glm::vec3(1.5f, -5.0f, -1.0f), glm::vec3(10.0f, 9.0f, 6.0f)));
		contextPtrs[i] = &m_gameContexts.back();
	}
	m_wolfInstance->setGameContexts(contextPtrs);
}

void SystemManager::loadSponzaScene()
{
	m_sponzaScene.reset(new SponzaScene(m_wolfInstance.get(), &m_mutex));

	m_gameState = GAME_STATE::RUNNING;
	m_needJoinLoadingThread = true;
}

void SystemManager::debugCallback(Wolf::Debug::Severity severity, Wolf::Debug::Type type, std::string message)
{
	if (severity == Wolf::Debug::Severity::VERBOSE)
		return;

	switch (severity)
	{
	case Wolf::Debug::Severity::ERROR:
		std::cout << "Error : ";
		break;
	case Wolf::Debug::Severity::WARNING:
		std::cout << "Warning : ";
		break;
	case Wolf::Debug::Severity::INFO:
		std::cout << "Info : ";
		break;
	}

	std::cout << message << std::endl;
}

ultralight::JSValue SystemManager::getFrameRate(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args)
{
	std::string fpsStr = "FPS: " + std::to_string(m_stableFPS);
	return ultralight::JSValue(fpsStr.c_str());
}