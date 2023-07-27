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

bool InputHandler::keyPressedThisFrame(int key)
{
	return std::ranges::find(m_keysPressedThisFrame, key) != m_keysPressedThisFrame.end();
}

void InputHandler::inputHandlerKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if(action == GLFW_PRESS)
	{
		const bool keyPressedPreviousFrame = std::ranges::find(m_keysPressedThisFrame, key) != m_keysPressedThisFrame.end();
		if (!keyPressedPreviousFrame && std::ranges::find(m_keysMaintained, key) == m_keysMaintained.end()) // key was released -> pressed this frame
		{
			m_keysPressedThisFrame.push_back(key);
		}
		else if(keyPressedPreviousFrame) // keys was pressed -> maintained
		{
			m_keysMaintained.push_back(key);
			std::erase(m_keysPressedThisFrame, key);
		}
	}
	else if(action == GLFW_RELEASE)
	{
		if(std::ranges::find(m_keysPressedThisFrame, key) != m_keysPressedThisFrame.end())
		{
			std::erase(m_keysPressedThisFrame, key);
		}
		else if(std::ranges::find(m_keysMaintained, key) != m_keysMaintained.end())
		{
			std::erase(m_keysMaintained, key);
		}
	}
}
