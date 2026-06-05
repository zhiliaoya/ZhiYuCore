// imgui_set_alpha.cpp
#include "imgui_set_alpha.h"

// 静态成员初始化
float ImGuiTransparentWindow::s_Alpha = 0.75f;  // 默认 75% 不透明度
bool ImGuiTransparentWindow::s_Initialized = false;

void ImGuiTransparentWindow::SetWindowAlpha(HWND hwnd, float alpha)
{
    if (!hwnd || !IsWindow(hwnd)) return;

    if (alpha < 1.0f)
    {
        LONG_PTR ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if ((ex_style & WS_EX_LAYERED) == 0)
        {
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED);
        }
        SetLayeredWindowAttributes(hwnd, 0, (BYTE)(255 * alpha), LWA_ALPHA);
    }
    else
    {
        LONG_PTR ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (ex_style & WS_EX_LAYERED)
        {
            SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex_style & ~WS_EX_LAYERED);
        }
    }
}

void ImGuiTransparentWindow::SetAlpha(float alpha)
{
    s_Alpha = ImClamp(alpha, 0.0f, 1.0f);

    // 立即更新所有已存在的窗口
    UpdateAllViewports();

    s_Initialized = true;
}

float ImGuiTransparentWindow::GetAlpha()
{
    return s_Alpha;
}

void ImGuiTransparentWindow::UpdateAllViewports()
{
    if (!s_Initialized || s_Alpha >= 1.0f) return;

    ImGuiIO& io = ImGui::GetIO();
    if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) return;

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    for (int i = 0; i < platform_io.Viewports.Size; i++)
    {
        ImGuiViewport* viewport = platform_io.Viewports[i];
        HWND hwnd = (HWND)viewport->PlatformHandle;

        // 跳过主窗口（主窗口使用 PushStyleColor 处理）
        // 但如果也想设置主窗口，可以去掉这个判断
        if (i > 0 && hwnd)
        {
            SetWindowAlpha(hwnd, s_Alpha);
        }
    }
}

void ImGuiTransparentWindow::PushWindowStyles()
{
    if (s_Alpha >= 1.0f) return;

    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    bg.w = s_Alpha;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.2f, 0.2f, 0.2f, s_Alpha));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.3f, 0.3f, 0.3f, s_Alpha));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.5f, 0.5f, s_Alpha));
}

void ImGuiTransparentWindow::PopWindowStyles()
{
    if (s_Alpha >= 1.0f) return;
    ImGui::PopStyleColor(4);
}

bool ImGuiTransparentWindow::Begin(const char* title, bool* p_open, ImGuiWindowFlags flags)
{
    PushWindowStyles();
    return ImGui::Begin(title, p_open, flags);
}

void ImGuiTransparentWindow::End()
{
    ImGui::End();
    PopWindowStyles();
}

void ImGuiTransparentWindow::UpdateViewport(ImGuiViewport* viewport)
{
    if (!s_Initialized || s_Alpha >= 1.0f) return;
    if (!viewport) return;

    HWND hwnd = (HWND)viewport->PlatformHandle;
    SetWindowAlpha(hwnd, s_Alpha);
}

void ImGuiTransparentWindow::OnViewportCreated(ImGuiViewport* viewport)
{
    if (!s_Initialized || s_Alpha >= 1.0f) return;
    UpdateViewport(viewport);
}

void ImGuiTransparentWindow::SetAllViewportsTopMost()
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    for (int i = 1; i < platform_io.Viewports.Size; i++)  // 跳过主视口（索引0）
    {
        ImGuiViewport* viewport = platform_io.Viewports[i];
        HWND hwnd = (HWND)viewport->PlatformHandle;
        if (hwnd && IsWindow(hwnd))
        {
            // 强制设置为置顶窗口
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}