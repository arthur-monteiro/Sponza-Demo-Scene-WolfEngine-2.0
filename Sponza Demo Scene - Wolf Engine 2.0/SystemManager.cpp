#include "SystemManager.h"

#include <iostream>

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
			std::vector<CommandRecordBase*> passes(1);
			passes[0] = m_loadingScreenUniquePass.get();

			m_wolfInstance->updateEvents();
			m_wolfInstance->frame(passes, m_loadingScreenUniquePass->getSemaphore());

			m_mutex.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		else if (m_gameState == GAME_STATE::RUNNING)
		{
			const uint32_t contextId = m_wolfInstance->getCurrentFrame() % g_configuration->getMaxCachedFrames();
			GameContext& gameContext = m_gameContexts[contextId];
			gameContext.sunDirection = -glm::vec3(glm::sin(m_sunPhi) * glm::cos(m_sunTheta), glm::cos(m_sunPhi), glm::sin(m_sunPhi) * glm::sin(m_sunTheta));
			gameContext.sunPhi = static_cast<float>(m_sunPhi);
			gameContext.sunTheta = static_cast<float>(m_sunTheta);
			gameContext.sunAreaAngle = static_cast<float>(m_sunAreaAngle);
			gameContext.enableTAA = m_TAAEnabled;

			m_sponzaScene->update(m_wolfInstance.get(), gameContext);
			m_sponzaScene->frame(m_wolfInstance.get());
		}

		const auto currentTime = std::chrono::steady_clock::now();
		m_currentFramesAccumulated++;
		const long long durationInMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - m_startTimeFPSCounter).count();
		if (durationInMs > 1000)
		{
			m_stableFPS = static_cast<uint32_t>(std::round((1000.0f * static_cast<float>(m_currentFramesAccumulated)) / static_cast<float>(durationInMs)));

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
	wolfInstanceCreateInfo.htmlURL = "UI/UI.html";
	wolfInstanceCreateInfo.bindUltralightCallbacks = [this] { bindUltralightCallbacks(); };
	wolfInstanceCreateInfo.useBindlessDescriptor = true;

	m_wolfInstance.reset(new WolfEngine(wolfInstanceCreateInfo));
	bindUltralightCallbacks();

	m_gameContexts.reserve(g_configuration->getMaxCachedFrames());
	std::vector<void*> contextPtrs(g_configuration->getMaxCachedFrames());
	for (uint32_t i = 0; i < g_configuration->getMaxCachedFrames(); ++i)
	{
		m_gameContexts.emplace_back(glm::vec3(1.5f, -5.0f, -1.0f), glm::vec3(10.0f, 9.0f, 6.0f));
		contextPtrs[i] = &m_gameContexts.back();
	}
	m_wolfInstance->setGameContexts(contextPtrs);
}

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#undef ERROR
#endif

void SystemManager::loadSponzaScene()
{
#ifdef _WIN32
	SetThreadDescription(GetCurrentThread(), L"Loading");
#endif

	m_sponzaScene.reset(new SponzaScene(m_wolfInstance.get(), &m_mutex));

	m_gameState = GAME_STATE::RUNNING;
	m_needJoinLoadingThread = true;
}

void SystemManager::debugCallback(Debug::Severity severity, Debug::Type type, const std::string& message)
{
	switch (severity)
	{
	case Debug::Severity::ERROR:
		std::cout << "Error : ";
		break;
	case Debug::Severity::WARNING:
		std::cout << "Warning : ";
		break;
	case Debug::Severity::INFO:
		std::cout << "Info : ";
		break;
	case Debug::Severity::VERBOSE:
		return;
	}

	std::cout << message << std::endl;
}

void SystemManager::bindUltralightCallbacks()
{
	ultralight::JSObject jsObject;
	m_wolfInstance->getUserInterfaceJSObject(jsObject);
	jsObject["getFrameRate"] = static_cast<ultralight::JSCallbackWithRetval>(std::bind(&SystemManager::getFrameRate, this, std::placeholders::_1, std::placeholders::_2));
	jsObject["setSunTheta"] = std::bind(&SystemManager::setSunTheta, this, std::placeholders::_1, std::placeholders::_2);
	jsObject["setSunPhi"] = std::bind(&SystemManager::setSunPhi, this, std::placeholders::_1, std::placeholders::_2);
	jsObject["setShadows"] = std::bind(&SystemManager::setShadows, this, std::placeholders::_1, std::placeholders::_2);
	jsObject["setSunAreaAngle"] = std::bind(&SystemManager::setSunAreaAngle, this, std::placeholders::_1, std::placeholders::_2);
	jsObject["setDebugMode"] = std::bind(&SystemManager::setDebugMode, this, std::placeholders::_1, std::placeholders::_2);
	jsObject["setEnableTAA"] = std::bind(&SystemManager::setEnableTAA, this, std::placeholders::_1, std::placeholders::_2);
}

ultralight::JSValue SystemManager::getFrameRate(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args)
{
	const std::string fpsStr = "FPS: " + std::to_string(m_stableFPS);
	return {fpsStr.c_str()};
}

void SystemManager::setSunTheta(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args)
{
	m_sunTheta = (args[0].ToNumber() * 2.0 * M_PI) - M_PI;
}

void SystemManager::setSunPhi(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args)
{
	m_sunPhi = (args[0].ToNumber() * M_PI) - M_PI_2;
}

void SystemManager::setShadows(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args)
{
	const std::string shadowType(static_cast<ultralight::String>(args[0].ToString()).utf8().data());
	if(shadowType == "Shadow Mapping")
		m_sponzaScene->setShadowType(SponzaScene::ShadowType::CSM);
	else if(shadowType == "Ray Tracing")
		m_sponzaScene->setShadowType(SponzaScene::ShadowType::RayTraced);
	else
		Debug::sendError("Unsupported shadow type");
}

void SystemManager::setSunAreaAngle(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args)
{
	m_sunAreaAngle = args[0].ToNumber() / 180.0;
}

void SystemManager::setDebugMode(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args)
{
	const std::string strDebugMode(static_cast<ultralight::String>(args[0].ToString()).utf8().data());

	ForwardPass::DebugMode debugMode = ForwardPass::DebugMode::None;
	if (strDebugMode == "none")
		debugMode = ForwardPass::DebugMode::None;
	else if (strDebugMode == "shadows")
		debugMode = ForwardPass::DebugMode::Shadows;
	else if (strDebugMode == "RTGI")
		debugMode = ForwardPass::DebugMode::RTGI;
	else
		Debug::sendError("Unsupported debug mode");

	m_sponzaScene->setDebugMode(debugMode);
}

void SystemManager::setEnableTAA(const ultralight::JSObject& thisObject, const ultralight::JSArgs& args)
{
	const std::string enable(static_cast<ultralight::String>(args[0].ToString()).utf8().data());

	if (enable == "true")
		m_TAAEnabled = true;
	else if (enable == "false")
		m_TAAEnabled = false;
	else
		Debug::sendError("Wrong input for set enable TAA");

}
