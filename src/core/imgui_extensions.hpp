#pragma once

#include "imgui.h"

namespace hex::ImGuiExt {

    bool BeginSubWindow(const char* label, bool* collapsed, ImVec2 size = ImVec2(0, 0), ImGuiChildFlags flags = ImGuiChildFlags_None);
    void EndSubWindow();
    void TextFormattedWrapped(const char* text);

}
