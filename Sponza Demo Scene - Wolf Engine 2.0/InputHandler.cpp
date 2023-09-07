#include "InputHandler.h"

static InputHandler* inputHandlerInstance;

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	inputHandlerInstance->inputHandlerKeyCallback(window, key, scancode, action, mods);
}

void InputHandler::initialize(GLFWwindow* window)
{
	inputHandlerInstance = this;

	glfwSetKeyCallback(window, keyCallback);
}

void InputHandler::moveToNextFrame()
{
	// Release key released
	for(int key : m_keysReleasedForNextFrame)
	{
		std::erase(m_keysPressedThisFrame, key);
		std::erase(m_keysMaintained, key);
	}

	// Move key pressed to maintained
	for(int key : m_keysPressedThisFrame)
	{
		m_keysMaintained.push_back(key);
	}

	m_keysPressedThisFrame.swap(m_keysPressedForNextFrame);
	m_keysPressedForNextFrame.clear();
}

bool InputHandler::keyPressedThisFrame(int key)
{
	return std::ranges::find(m_keysPressedThisFrame, key) != m_keysPressedThisFrame.end();
}

void InputHandler::inputHandlerKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if(action == GLFW_PRESS)
	{
		m_keysPressedForNextFrame.push_back(key);
	}
	else if(action == GLFW_RELEASE)
	{
		m_keysReleasedForNextFrame.push_back(key);
	}
}
