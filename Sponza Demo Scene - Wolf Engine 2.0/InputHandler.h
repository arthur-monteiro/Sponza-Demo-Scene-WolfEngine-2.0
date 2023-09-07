#pragma once

#include <GLFW/glfw3.h>
#include <vector>

#include <InputHandlerInterface.h>

class InputHandler : public Wolf::InputHandlerInterface
{
public:
	void initialize(GLFWwindow* window) override;
	void moveToNextFrame() override;

	bool keyPressedThisFrame(int key);

	void inputHandlerKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

private:
	std::vector<int> m_keysPressedForNextFrame;
	std::vector<int> m_keysReleasedForNextFrame;

	std::vector<int> m_keysPressedThisFrame;
	std::vector<int> m_keysMaintained;
};

