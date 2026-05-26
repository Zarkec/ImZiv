#pragma once

#include <GLFW/glfw3.h>

#include <string>

std::string openFileDialog(GLFWwindow* window);
std::string saveFileDialog(GLFWwindow* window, const std::string& suggestedName);
std::string selectFolderDialog(GLFWwindow* window);
std::string openWorkflowDialog(GLFWwindow* window);
std::string saveWorkflowDialog(GLFWwindow* window, const std::string& suggestedName);
