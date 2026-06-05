// imgui_set_alpha.h
#pragma once
#include "imgui.h"
#include "imgui_impl_win32.h"
#include <windows.h>
#include "imgui_internal.h"

class ImGuiTransparentWindow
{
private:
    static float s_Alpha;
    static bool s_Initialized;

    // 直接操作 Win32 窗口设置透明度
    static void SetWindowAlpha(HWND hwnd, float alpha);

public:
    // 设置全局透明度 (0.0f = 完全透明, 1.0f = 完全不透明)
    static void SetAlpha(float alpha);

    static float GetAlpha();

    // 更新所有浮动窗口的透明度
    static void UpdateAllViewports();

    // 为当前 ImGui 窗口应用透明度样式
    static void PushWindowStyles();

    static void PopWindowStyles();

    // 便捷函数：一行代码完成 Begin + 样式
    static bool Begin(const char* title, bool* p_open = nullptr, ImGuiWindowFlags flags = 0);

    static void End();

    // 手动更新特定窗口
    static void UpdateViewport(ImGuiViewport* viewport);

    // 监听窗口创建（需要在消息循环中调用）
    static void OnViewportCreated(ImGuiViewport* viewport);

    static void SetAllViewportsTopMost();
};