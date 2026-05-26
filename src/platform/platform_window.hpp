#pragma once

#include <GLFW/glfw3.h>

#include <cstdint>
#include <string>
#include <vector>

struct NativeDropData {
    std::string value;
    std::vector<uint8_t> bytes;
};

using NativeDropCallback = void (*)(NativeDropData data);

void setupBorderlessWindow(GLFWwindow* window);
void applyBorderlessWindowStyle(GLFWwindow* window);
void setTitleBarHeight(float height);
void invalidateWindow(GLFWwindow* window);
void flushWindowFrame();
void setWindowIcon(GLFWwindow* window);
void applyWindowSizeLimits(GLFWwindow* window);
void centerWindowOnPrimaryMonitor(GLFWwindow* window);
bool isWindowTopMost(GLFWwindow* window);
void setWindowTopMost(GLFWwindow* window, bool topMost);
void setupNativeDropTarget(GLFWwindow* window, NativeDropCallback callback);
void shutdownNativeDropTarget(GLFWwindow* window);
