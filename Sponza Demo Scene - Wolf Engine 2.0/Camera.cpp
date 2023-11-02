#include "Camera.h"

Camera::Camera(glm::vec3 position, glm::vec3 target, glm::vec3 verticalAxis, float sensibility, float speed, float aspect)
{
	m_position = position;
	m_target = target;
	m_verticalAxis = verticalAxis;
	m_sensibility = sensibility;
	m_speed = speed;
	m_aspect = aspect;
}

void Camera::update(GLFWwindow* window)
{
	if (m_overrideViewMatrices)
		return;

	if (m_oldMousePosX < 0 || m_locked)
	{
		glfwGetCursorPos(window, &m_oldMousePosX, &m_oldMousePosY);
		return;
	}

	const auto currentTime = std::chrono::high_resolution_clock::now();
	const long long microsecondOffset = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - m_startTime).count();
	const float secondOffset = static_cast<float>(microsecondOffset) / 1'000'000.0f;
	m_startTime = currentTime;

	double currentMousePosX, currentMousePosY;
	glfwGetCursorPos(window, &currentMousePosX, &currentMousePosY);

	updateOrientation(static_cast<float>(currentMousePosX - m_oldMousePosX), static_cast<float>(currentMousePosY - m_oldMousePosY));
	m_oldMousePosX = currentMousePosX;
	m_oldMousePosY = currentMousePosY;

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
	{
		m_position = m_position + m_orientation * (secondOffset * m_speed);
		m_target = m_position + m_orientation;
	}
	else if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
	{
		m_position = m_position - m_orientation * (secondOffset * m_speed);
		m_target = m_position + m_orientation;
	}

	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
	{
		m_position = m_position + m_lateralDirection * (secondOffset * m_speed);
		m_target = m_position + m_orientation;
	}

	else if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
	{
		m_position = m_position - m_lateralDirection * (secondOffset * m_speed);
		m_target = m_position + m_orientation;
	}

	m_previousViewMatrix = m_viewMatrix;
	m_viewMatrix = glm::lookAt(m_position, m_target, m_verticalAxis);

	m_projectionMatrix = glm::perspective(m_radFOV, m_aspect, m_near, m_far);
	m_projectionMatrix[1][1] *= -1;
}

const glm::mat4& Camera::getViewMatrix() const
{
	return m_viewMatrix;
}

const glm::mat4& Camera::getPreviousViewMatrix() const
{
	return m_previousViewMatrix;
}

glm::vec3 Camera::getPosition() const
{
	return m_position;
}

const glm::mat4& Camera::getProjectionMatrix() const
{
	if (m_overrideViewMatrices)
		return m_overridenProjectionMatrix;

	return m_projectionMatrix;
}

void Camera::overrideMatrices(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
{
	m_viewMatrix = viewMatrix;
	m_previousViewMatrix = viewMatrix;
	m_overridenProjectionMatrix = projectionMatrix;

	m_overrideViewMatrices = true;
}

void Camera::updateOrientation(float xOffset, float yOffset)
{
	m_phi -= yOffset * m_sensibility;
	m_theta -= xOffset * m_sensibility;

	if (m_phi > glm::half_pi<float>() - OFFSET_ANGLES)
		m_phi = glm::half_pi<float>() - OFFSET_ANGLES;
	else if (m_phi < -glm::half_pi<float>() + OFFSET_ANGLES)
		m_phi = -glm::half_pi<float>() + OFFSET_ANGLES;

	if (m_verticalAxis.x == 1.0)
	{
		m_orientation.x = glm::sin(m_phi);
		m_orientation.y = glm::cos(m_phi) * glm::cos(m_theta);
		m_orientation.z = glm::cos(m_phi) * glm::sin(m_theta);
	}

	else if (m_verticalAxis.y == 1.0)
	{
		m_orientation.x = glm::cos(m_phi) * glm::sin(m_theta);
		m_orientation.y = glm::sin(m_phi);
		m_orientation.z = glm::cos(m_phi) * glm::cos(m_theta);
	}

	else
	{
		m_orientation.x = glm::cos(m_phi) * glm::cos(m_theta);
		m_orientation.y = glm::cos(m_phi) * glm::sin(m_theta);
		m_orientation.z = glm::sin(m_phi);
	}

	m_lateralDirection = glm::normalize(glm::cross(m_verticalAxis, m_orientation));
	m_target = m_position + m_orientation;
}
