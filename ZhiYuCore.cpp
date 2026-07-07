#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_set_alpha.h"
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <Windows.h>
#include <winhttp.h>
#include <fstream>
#include <shlobj.h>
#include "nlohmann/json.hpp"

// 用于链接WinHTTP库
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

using json = nlohmann::json;

// Data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// 智能体模式枚举
enum class AgentMode {
    Translation,    // 翻译模式
    Analysis,       // 分析模式
    Summary,        // 总结模式
    Polish,         // 润色模式
    Custom          // 自定义模式
};

// 剪贴板和翻译相关变量
static std::string g_clipboard_content;
static std::string g_translated_content;
static std::string g_source_language = "自动检测";
static bool g_show_translation_window = false;
static bool g_show_settings_window = false;
static bool g_monitoring = true;
static bool g_translating = false;
static bool g_translation_error = false;
static std::string g_error_message;
static HHOOK g_mouse_hook = nullptr;
static HHOOK g_keyboard_hook = nullptr;
static POINT g_mouse_pos = { 0, 0 };
static std::mutex g_translation_mutex;

// 字体大小控制变量
static float g_font_scale = 1.0f;
static float g_min_font_scale = 0.5f;
static float g_max_font_scale = 3.0f;
static float g_base_font_size = 25.0f;
static ImFont* g_current_font = nullptr;
static ImFont* g_fonts[10] = { nullptr };
static int g_font_count = 0;

// 窗口透明度控制
static float g_window_alpha = 0.85f;

// 用户配置变量
static std::string g_api_key = "";
static std::string g_api_url = "https://api.deepseek.com";
static std::string g_api_path = "/chat/completions";
static std::string g_model = "deepseek-chat";
static float g_temperature = 0.3f;
static int g_max_tokens = 4096;
static int g_timeout = 30000;

// 系统提示词变量
static std::string g_system_prompt =
"你是一个专业的翻译助手。请将用户提供的文本翻译成{target_language}。\n\n"
"要求：\n"
"1. 保持原文的准确性和专业性\n"
"2. 确保翻译自然流畅\n"
"3. 只返回翻译结果，不添加任何解释";

static std::string g_target_language = "简体中文";

// 模式相关变量
static AgentMode g_current_mode = AgentMode::Translation;
static std::string g_mode_display_name = "翻译";
static bool g_show_original_text = true;

// 配置文件路径
static std::string g_config_path = "";

// 输入框缓冲区
static char g_api_key_buffer[256] = { 0 };
static char g_api_url_buffer[256] = { 0 };
static char g_model_buffer[128] = { 0 };
static char g_target_language_buffer[128] = { 0 };
static char g_system_prompt_buffer[4096] = { 0 };

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
std::string TranslateWithDeepSeek(const std::string& text);
void AsyncTranslate(const std::string& text);
void ToggleMonitoring();
void ShowNotification(const std::string& title, const std::string& message);
std::string GetConfigPath();
void LoadConfig();
void SaveConfig();
void DetectModeFromPrompt();
void SetAgentMode(AgentMode mode, const std::string& custom_prompt = "");

// 辅助字符串转换函数
std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

// 获取程序所在目录
std::string GetProgramDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring wPath(path);
    std::wstring wDir = wPath.substr(0, wPath.find_last_of(L"\\/"));
    return WStringToString(wDir);
}

// 获取配置文件路径
std::string GetConfigPath() {
    if (!g_config_path.empty()) {
        return g_config_path;
    }
    std::string dir = GetProgramDirectory();
    return dir + "\\zhiyu_config.json";
}

// 保存配置文件
void SaveConfig() {
    std::string configPath = GetConfigPath();

    json config;

    // API配置
    config["api_key"] = g_api_key;
    config["api_url"] = g_api_url;
    config["api_path"] = g_api_path;
    config["model"] = g_model;
    config["temperature"] = g_temperature;
    config["max_tokens"] = g_max_tokens;
    config["timeout"] = g_timeout;

    // 界面配置
    config["font_scale"] = g_font_scale;
    config["window_alpha"] = g_window_alpha;

    // 翻译配置
    config["system_prompt"] = g_system_prompt;
    config["target_language"] = g_target_language;

    // 模式配置
    config["agent_mode"] = static_cast<int>(g_current_mode);
    config["mode_display_name"] = g_mode_display_name;

    try {
        std::ofstream file(configPath);
        if (file.is_open()) {
            file << config.dump(4);
            file.close();
        }
    }
    catch (const std::exception& e) {
        // 保存失败，忽略错误
    }
}

// 复制函数
bool CopyToClipboard(const std::string& text) {
    if (text.empty()) return false;

    if (!OpenClipboard(nullptr)) return false;

    // 先清空剪贴板
    EmptyClipboard();

    // 转换字符串
    std::wstring wContent = StringToWString(text);
    size_t size = (wContent.length() + 1) * sizeof(wchar_t);

    // 分配内存
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (hMem) {
        // 锁定内存并复制
        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
        if (pMem) {
            memcpy(pMem, wContent.c_str(), size);
            GlobalUnlock(hMem);

            // 设置剪贴板数据
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        else {
            GlobalFree(hMem);
        }
    }

    CloseClipboard();
    return true;
}

// 根据提示词内容自动检测模式
void DetectModeFromPrompt() {
    std::string lower_prompt = g_system_prompt;
    std::transform(lower_prompt.begin(), lower_prompt.end(), lower_prompt.begin(), ::tolower);

    if (g_system_prompt.find("翻译") != std::string::npos &&
        (g_system_prompt.find("翻译成") != std::string::npos ||
            g_system_prompt.find("translate") != std::string::npos)) {
        g_current_mode = AgentMode::Translation;
        g_mode_display_name = "翻译";
        g_show_original_text = true;
    }
    else if (g_system_prompt.find("分析") != std::string::npos ||
        g_system_prompt.find("分析文本") != std::string::npos) {
        g_current_mode = AgentMode::Analysis;
        g_mode_display_name = "分析";
        g_show_original_text = false;
    }
    else if (g_system_prompt.find("总结") != std::string::npos ||
        g_system_prompt.find("summar") != std::string::npos) {
        g_current_mode = AgentMode::Summary;
        g_mode_display_name = "总结";
        g_show_original_text = false;
    }
    else if (g_system_prompt.find("润色") != std::string::npos ||
        g_system_prompt.find("polish") != std::string::npos) {
        g_current_mode = AgentMode::Polish;
        g_mode_display_name = "润色";
        g_show_original_text = false;
    }
    else {
        g_current_mode = AgentMode::Custom;
        g_mode_display_name = "处理";
        g_show_original_text = false;
    }
}

// 设置模式
void SetAgentMode(AgentMode mode, const std::string& custom_prompt) {
    g_current_mode = mode;

    switch (mode) {
    case AgentMode::Translation:
        g_mode_display_name = "翻译";
        g_show_original_text = true;
        g_system_prompt =
            "你是一个专业的翻译助手。请将用户提供的文本翻译成{target_language}。\n\n"
            "要求：\n"
            "1. 保持原文的准确性和专业性\n"
            "2. 确保翻译自然流畅\n"
            "3. 只返回翻译结果，不添加任何解释";
        break;

    case AgentMode::Analysis:
        g_mode_display_name = "分析";
        g_show_original_text = false;
        g_system_prompt =
            "你是一个文本分析专家。请分析用户提供的文本，输出以下内容：\n\n"
            "【核心主题】\n"
            "【情感倾向】\n"
            "【关键要点】\n\n"
            "请保持分析简洁专业，只返回分析结果。";
        break;

    case AgentMode::Summary:
        g_mode_display_name = "总结";
        g_show_original_text = false;
        g_system_prompt =
            "请总结用户提供的文本的核心内容，用3-5个要点概括。\n\n"
            "要求：\n"
            "1. 提取关键信息\n"
            "2. 保持客观准确\n"
            "3. 按重要性排序\n"
            "4. 只返回总结结果";
        break;

    case AgentMode::Polish:
        g_mode_display_name = "润色";
        g_show_original_text = false;
        g_system_prompt =
            "你是一个专业的文字润色助手。请润色用户提供的文本。\n\n"
            "要求：\n"
            "1. 保持原文的核心意思\n"
            "2. 优化表达方式\n"
            "3. 修正语法错误\n"
            "4. 只返回润色后的结果";
        break;

    case AgentMode::Custom:
        g_mode_display_name = "处理";
        g_show_original_text = false;
        if (!custom_prompt.empty()) {
            g_system_prompt = custom_prompt;
        }
        break;
    }

    // 更新缓冲区
    strncpy_s(g_system_prompt_buffer, g_system_prompt.c_str(), sizeof(g_system_prompt_buffer) - 1);
    SaveConfig();
}

// 加载配置文件
void LoadConfig() {
    std::string configPath = GetConfigPath();

    std::ifstream file(configPath);
    if (!file.is_open()) {
        // 配置文件不存在，使用默认值
        g_api_key = "";
        g_api_url = "https://api.deepseek.com";
        g_api_path = "/chat/completions";
        g_model = "deepseek-chat";
        g_temperature = 0.3f;
        g_max_tokens = 4096;
        g_timeout = 30000;
        g_font_scale = 1.0f;
        g_window_alpha = 0.85f;
        g_target_language = "简体中文";

        // 初始化默认提示词
        g_system_prompt =
            "你是一个专业的翻译助手。请将用户提供的文本翻译成{target_language}。\n\n"
            "要求：\n"
            "1. 保持原文的准确性和专业性\n"
            "2. 确保翻译自然流畅\n"
            "3. 只返回翻译结果，不添加任何解释";

        g_current_mode = AgentMode::Translation;
        g_mode_display_name = "翻译";
        g_show_original_text = true;

        // 初始化输入缓冲区
        strncpy_s(g_api_key_buffer, g_api_key.c_str(), sizeof(g_api_key_buffer) - 1);
        strncpy_s(g_api_url_buffer, g_api_url.c_str(), sizeof(g_api_url_buffer) - 1);
        strncpy_s(g_model_buffer, g_model.c_str(), sizeof(g_model_buffer) - 1);
        strncpy_s(g_target_language_buffer, g_target_language.c_str(), sizeof(g_target_language_buffer) - 1);
        strncpy_s(g_system_prompt_buffer, g_system_prompt.c_str(), sizeof(g_system_prompt_buffer) - 1);

        // 创建默认配置文件
        SaveConfig();
        return;
    }

    try {
        json config = json::parse(file);
        file.close();

        // 加载API配置
        if (config.contains("api_key")) g_api_key = config["api_key"].get<std::string>();
        if (config.contains("api_url")) g_api_url = config["api_url"].get<std::string>();
        if (config.contains("api_path")) g_api_path = config["api_path"].get<std::string>();
        if (config.contains("model")) g_model = config["model"].get<std::string>();
        if (config.contains("temperature")) g_temperature = config["temperature"].get<float>();
        if (config.contains("max_tokens")) g_max_tokens = config["max_tokens"].get<int>();
        if (config.contains("timeout")) g_timeout = config["timeout"].get<int>();

        // 加载界面配置
        if (config.contains("font_scale")) g_font_scale = config["font_scale"].get<float>();
        if (config.contains("window_alpha")) g_window_alpha = config["window_alpha"].get<float>();

        // 加载翻译配置
        if (config.contains("system_prompt")) g_system_prompt = config["system_prompt"].get<std::string>();
        if (config.contains("target_language")) g_target_language = config["target_language"].get<std::string>();

        // 加载模式配置
        if (config.contains("agent_mode")) {
            int mode = config["agent_mode"].get<int>();
            g_current_mode = static_cast<AgentMode>(mode);
        }
        if (config.contains("mode_display_name")) g_mode_display_name = config["mode_display_name"].get<std::string>();

        // 初始化输入缓冲区
        strncpy_s(g_api_key_buffer, g_api_key.c_str(), sizeof(g_api_key_buffer) - 1);
        strncpy_s(g_api_url_buffer, g_api_url.c_str(), sizeof(g_api_url_buffer) - 1);
        strncpy_s(g_model_buffer, g_model.c_str(), sizeof(g_model_buffer) - 1);
        strncpy_s(g_target_language_buffer, g_target_language.c_str(), sizeof(g_target_language_buffer) - 1);
        strncpy_s(g_system_prompt_buffer, g_system_prompt.c_str(), sizeof(g_system_prompt_buffer) - 1);

        // 根据提示词更新显示标志
        DetectModeFromPrompt();
    }
    catch (const json::exception& e) {
        // 配置文件解析失败，使用默认值
        g_api_key = "";
        g_api_url = "https://api.deepseek.com";
        g_model = "deepseek-chat";
        g_target_language = "简体中文";

        strncpy_s(g_api_key_buffer, g_api_key.c_str(), sizeof(g_api_key_buffer) - 1);
        strncpy_s(g_api_url_buffer, g_api_url.c_str(), sizeof(g_api_url_buffer) - 1);
        strncpy_s(g_model_buffer, g_model.c_str(), sizeof(g_model_buffer) - 1);
        strncpy_s(g_target_language_buffer, g_target_language.c_str(), sizeof(g_target_language_buffer) - 1);
        strncpy_s(g_system_prompt_buffer, g_system_prompt.c_str(), sizeof(g_system_prompt_buffer) - 1);
    }
}

// 显示系统托盘通知
void ShowNotification(const std::string& title, const std::string& message) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.uFlags = NIF_INFO | NIF_MESSAGE;
    nid.dwInfoFlags = NIIF_INFO;

    std::wstring wTitle = StringToWString(title);
    std::wstring wMessage = StringToWString(message);

    wcsncpy_s(nid.szInfoTitle, wTitle.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, wMessage.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);

    if (title.find("开启") != std::string::npos) {
        MessageBeep(MB_ICONINFORMATION);
    }
    else {
        MessageBeep(MB_ICONEXCLAMATION);
    }
}

// 更新字体大小
void UpdateFontSize(float scale) {
    if (scale < g_min_font_scale) scale = g_min_font_scale;
    if (scale > g_max_font_scale) scale = g_max_font_scale;

    g_font_scale = scale;
    float target_size = g_base_font_size * g_font_scale;

    ImFont* best_font = nullptr;
    float best_diff = FLT_MAX;

    for (int i = 0; i < g_font_count; i++) {
        if (g_fonts[i] != nullptr) {
            float font_size = g_base_font_size * (1.0f + i * 0.3f);
            float diff = fabsf(font_size - target_size);
            if (diff < best_diff) {
                best_diff = diff;
                best_font = g_fonts[i];
            }
        }
    }

    if (best_font != nullptr) {
        g_current_font = best_font;
        ImGui::GetIO().FontDefault = best_font;
    }

    // 保存配置
    SaveConfig();
}

// 切换监听状态
void ToggleMonitoring() {
    g_monitoring = !g_monitoring;

    if (g_monitoring) {
        ShowNotification("智语Core 监听已开启", "点击鼠标中键处理选中文本");
    }
    else {
        ShowNotification("智语Core 监听已停止", "按F1重新开启监听");
    }
}

// 获取剪贴板文本
std::string GetClipboardText() {
    std::string text;
    if (OpenClipboard(nullptr)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pwszText = static_cast<wchar_t*>(GlobalLock(hData));
            if (pwszText) {
                text = WStringToString(pwszText);
                GlobalUnlock(hData);
            }
        }
        else {
            hData = GetClipboardData(CF_TEXT);
            if (hData) {
                char* pszText = static_cast<char*>(GlobalLock(hData));
                if (pszText) {
                    // CF_TEXT 是系统ANSI代码页编码（如中文系统下的GBK），
                    // 必须先转成宽字符再转成UTF-8，否则含中文时会产生非法UTF-8字节，
                    // 导致后续 json.dump() 抛出 type_error.316
                    int wlen = MultiByteToWideChar(CP_ACP, 0, pszText, -1, NULL, 0);
                    if (wlen > 0) {
                        std::wstring wtext(wlen, 0);
                        MultiByteToWideChar(CP_ACP, 0, pszText, -1, &wtext[0], wlen);
                        // 去掉MultiByteToWideChar在-1模式下带出的结尾空字符
                        if (!wtext.empty() && wtext.back() == L'\0') wtext.pop_back();
                        text = WStringToString(wtext);
                    }
                    GlobalUnlock(hData);
                }
            }
        }
        CloseClipboard();
    }
    return text;
}

// 模拟Ctrl+C
void SimulateCtrlC() {
    keybd_event(VK_CONTROL, 0x9d, 0, 0);
    keybd_event('C', 0x9e, 0, 0);
    keybd_event('C', 0x9e, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0x9d, KEYEVENTF_KEYUP, 0);
}

// 清理字符串中的非法UTF-8字节，防止 json::dump() 抛出 type_error.316
// 采用UTF-8状态机逐字节校验，遇到非法序列直接丢弃该字节
std::string SanitizeUtf8(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    size_t i = 0, n = input.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        int len = 0;
        if ((c & 0x80) == 0x00) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else { i++; continue; } // 非法起始字节，丢弃

        if (i + len > n) { i++; continue; } // 序列不完整，丢弃

        bool valid = true;
        for (int k = 1; k < len; k++) {
            unsigned char cc = static_cast<unsigned char>(input[i + k]);
            if ((cc & 0xC0) != 0x80) { valid = false; break; }
        }

        if (valid) {
            out.append(input, i, len);
            i += len;
        }
        else {
            i++; // 丢弃这个非法起始字节，从下一个字节重试
        }
    }
    return out;
}

// DeepSeek API调用函数
std::string TranslateWithDeepSeek(const std::string& text) {
    if (text.empty()) return "";
    if (g_api_key.empty()) return "错误：请先设置API Key";

    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;

    try {
        json requestBody;
        requestBody["model"] = g_model;
        requestBody["stream"] = false;
        requestBody["temperature"] = g_temperature;
        requestBody["max_tokens"] = g_max_tokens;

        json messages = json::array();

        // 使用配置文件中的系统提示词
        json systemMessage;
        systemMessage["role"] = "system";

        // 构造系统提示词，动态插入目标语言
        std::string final_system_prompt = g_system_prompt;

        // 如果提示词中包含 {target_language} 占位符，则替换它
        size_t pos = final_system_prompt.find("{target_language}");
        if (pos != std::string::npos) {
            final_system_prompt.replace(pos, 18, g_target_language);
        }
        // 如果是翻译模式但没有占位符，在末尾添加
        else if (g_current_mode == AgentMode::Translation) {
            final_system_prompt += "\n\n请将文本翻译为" + g_target_language + "。";
        }

        systemMessage["content"] = SanitizeUtf8(final_system_prompt);
        messages.push_back(systemMessage);

        json userMessage;
        userMessage["role"] = "user";
        userMessage["content"] = SanitizeUtf8(text);
        messages.push_back(userMessage);

        requestBody["messages"] = messages;
        std::string requestStr = requestBody.dump();

        hSession = WinHttpOpen(L"ZhiYuCore/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);

        if (!hSession) return "错误：无法初始化网络连接";

        DWORD dwTimeout = g_timeout;
        WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
        WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
        WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));

        // 从URL中提取主机名
        std::string host = g_api_url;
        if (host.find("https://") == 0) host = host.substr(8);
        else if (host.find("http://") == 0) host = host.substr(7);

        std::wstring wHost = StringToWString(host);
        std::wstring wPath = StringToWString(g_api_path);

        hConnect = WinHttpConnect(hSession, wHost.c_str(),
            INTERNET_DEFAULT_HTTPS_PORT, 0);

        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return "错误：无法连接服务器";
        }

        hRequest = WinHttpOpenRequest(hConnect, L"POST", wPath.c_str(),
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);

        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "错误：无法创建请求";
        }

        std::wstring authHeader = L"Authorization: Bearer " + StringToWString(g_api_key);
        LPCWSTR headers[] = {
            L"Content-Type: application/json",
            authHeader.c_str()
        };

        if (!WinHttpAddRequestHeaders(hRequest, headers[0], -1, WINHTTP_ADDREQ_FLAG_ADD) ||
            !WinHttpAddRequestHeaders(hRequest, headers[1], -1, WINHTTP_ADDREQ_FLAG_ADD)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "错误：无法设置请求头";
        }

        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)requestStr.c_str(),
            (DWORD)requestStr.length(),
            (DWORD)requestStr.length(), 0)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "错误：发送请求失败";
        }

        if (!WinHttpReceiveResponse(hRequest, nullptr)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "错误：接收响应失败";
        }

        DWORD dwStatusCode = 0;
        DWORD dwStatusCodeSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &dwStatusCode, &dwStatusCodeSize, WINHTTP_NO_HEADER_INDEX);

        std::string response;
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;

        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;

            std::vector<char> buffer(dwSize + 1, 0);
            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                response.append(buffer.data(), dwDownloaded);
            }
            else {
                break;
            }
        } while (dwSize > 0);

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        if (dwStatusCode != 200) {
            try {
                json errorJson = json::parse(response);
                if (errorJson.contains("error") && errorJson["error"].contains("message")) {
                    return "API错误(" + std::to_string(dwStatusCode) + "): " + errorJson["error"]["message"].get<std::string>();
                }
            }
            catch (...) {}
            return "HTTP错误: " + std::to_string(dwStatusCode);
        }

        if (response.empty()) return "错误：服务器返回空响应";

        json responseJson = json::parse(response);

        if (responseJson.contains("choices") &&
            responseJson["choices"].is_array() &&
            !responseJson["choices"].empty()) {

            auto& choice = responseJson["choices"][0];
            if (choice.contains("message") && choice["message"].contains("content")) {
                std::string translatedText = choice["message"]["content"];

                if (!translatedText.empty()) {
                    size_t start = translatedText.find_first_not_of(" \t\n\r\"'");
                    size_t end = translatedText.find_last_not_of(" \t\n\r\"'");
                    if (start != std::string::npos && end != std::string::npos) {
                        translatedText = translatedText.substr(start, end - start + 1);
                    }
                }
                return translatedText;
            }
        }

        if (responseJson.contains("error")) {
            return "API错误: " + responseJson["error"].get<std::string>();
        }

        return "错误：无法解析处理结果";
    }
    catch (const json::parse_error& e) {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
        return "JSON解析错误: " + std::string(e.what());
    }
    catch (const std::exception& e) {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
        return "未知错误: " + std::string(e.what());
    }
    catch (...) {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
        return "发生未知异常";
    }
}

// 异步处理线程函数
void AsyncTranslate(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_translation_mutex);

    g_translating = true;
    g_translation_error = false;
    g_translated_content = g_mode_display_name + "中...";

    std::thread([text]() {
        std::string result = TranslateWithDeepSeek(text);

        std::lock_guard<std::mutex> lock(g_translation_mutex);
        g_translating = false;

        if (result.find("错误") != std::string::npos ||
            result.find("error") != std::string::npos) {
            g_translation_error = true;
            g_error_message = result;
            g_translated_content = g_mode_display_name + "失败: " + result;
        }
        else {
            g_translation_error = false;
            g_translated_content = result;
        }
        }).detach();
}

// 鼠标钩子回调函数
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_monitoring) {
        if (wParam == WM_MBUTTONDOWN) {
            MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
            g_mouse_pos = pMouseStruct->pt;

            // 记录模拟复制前的剪贴板序列号，用于判断本次点击是否真的复制到了新内容。
            // 如果点击处没有选中文本，Ctrl+C 不会写入剪贴板，序列号不会变，
            // 此时不应该用旧的剪贴板内容再次触发AI处理。
            DWORD seqBefore = GetClipboardSequenceNumber();

            Sleep(50);
            SimulateCtrlC();
            Sleep(100);

            DWORD seqAfter = GetClipboardSequenceNumber();
            if (seqAfter == seqBefore) {
                // 没有产生新的复制动作（未选中文本），跳过本次处理
                return CallNextHookEx(g_mouse_hook, nCode, wParam, lParam);
            }

            std::string content = GetClipboardText();

            if (!content.empty()) {
                g_clipboard_content = content;
                g_show_translation_window = true;
                AsyncTranslate(content);
            }
        }
    }
    return CallNextHookEx(g_mouse_hook, nCode, wParam, lParam);
}

// 键盘钩子回调函数
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        if (wParam == WM_KEYDOWN) {
            KBDLLHOOKSTRUCT* pKeyStruct = (KBDLLHOOKSTRUCT*)lParam;

            if (!(pKeyStruct->flags & LLKHF_UP)) {
                switch (pKeyStruct->vkCode) {
                case VK_F1:
                    ToggleMonitoring();
                    break;

                case VK_F2:
                    if (!g_clipboard_content.empty()) {
                        g_show_translation_window = !g_show_translation_window;
                    }
                    break;

                case VK_F3:
                    g_show_settings_window = !g_show_settings_window;
                    if (g_show_settings_window) {
                        ShowNotification("设置窗口已打开", "可以调整API参数和智能体设置");
                    }
                    break;

                case VK_ESCAPE:
                    g_show_translation_window = false;
                    g_show_settings_window = false;
                    break;
                }
            }
        }
    }
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
}

// Main code
int WINAPI WinMain(HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    // 加载配置文件
    LoadConfig();

    // 应用加载的配置
    ImGuiTransparentWindow::SetAlpha(g_window_alpha);

    // Make process DPI aware
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create a small hidden window for D3D
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ZhiYuCore", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"智语Core", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Hide the main window completely
    ::ShowWindow(hwnd, SW_HIDE);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoAutoMerge = true;
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // 加载字体
    ImFontConfig font_config;
    font_config.MergeMode = false;

    float font_sizes[] = { 15.0f, 20.0f, 25.0f, 30.0f, 35.0f, 40.0f, 50.0f, 60.0f, 75.0f };
    g_font_count = sizeof(font_sizes) / sizeof(font_sizes[0]);

    for (int i = 0; i < g_font_count; i++) {
        g_fonts[i] = io.Fonts->AddFontFromFileTTF(
            "c:\\Windows\\Fonts\\msyh.ttc",
            font_sizes[i],
            &font_config,
            io.Fonts->GetGlyphRangesChineseFull()
        );
    }

    g_current_font = g_fonts[2];
    io.FontDefault = g_current_font;

    // 设置钩子
    g_mouse_hook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, GetModuleHandle(nullptr), 0);
    g_keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(nullptr), 0);

    // 启动提示
    if (g_api_key.empty()) {
        ShowNotification("智语Core 已启动", "请按F3打开设置，配置API Key");
    }
    else {
        ShowNotification("智语Core 已启动", "F1:监听开关 | F2:工作窗口 | F3:设置 | ESC:关闭窗口");
    }

    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_current_font != nullptr) {
            ImGui::PushFont(g_current_font);
        }

        // ============ 设置窗口 ============
        if (g_show_settings_window) {
            ImGui::SetNextWindowPos(ImVec2(100, 50), ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(ImVec2(650, 850), ImGuiCond_Appearing);

            ImGuiTransparentWindow::PushWindowStyles();
            ImGui::Begin("智语Core 设置", &g_show_settings_window);

            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "智语Core 设置");
            ImGui::Separator();
            ImGui::Spacing();

            // 使用Tab标签页组织界面
            if (ImGui::BeginTabBar("SettingsTabs")) {

                // ============ API设置标签页 ============
                if (ImGui::BeginTabItem("DeepSeek API设置")) {
                    ImGui::Spacing();

                    // API Key输入框
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "API Key (必填):");
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::InputText("##ApiKey", g_api_key_buffer, sizeof(g_api_key_buffer), ImGuiInputTextFlags_Password)) {
                        g_api_key = g_api_key_buffer;
                        SaveConfig();
                    }
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "请输入您的API Key");

                    ImGui::Spacing();

                    // API URL输入框
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "API URL:");
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::InputText("##ApiUrl", g_api_url_buffer, sizeof(g_api_url_buffer))) {
                        g_api_url = g_api_url_buffer;
                        SaveConfig();
                    }

                    ImGui::Spacing();

                    // 模型选择
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "模型:");
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::InputText("##Model", g_model_buffer, sizeof(g_model_buffer))) {
                        g_model = g_model_buffer;
                        SaveConfig();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // 温度参数
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "温度 (Temperature):");
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::SliderFloat("##Temperature", &g_temperature, 0.0f, 2.0f, "%.2f")) {
                        SaveConfig();
                    }
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "越低越确定，越高越随机 (推荐: 0.3)");

                    ImGui::Spacing();

                    // 最大Token数
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "最大Token数:");
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    int max_tokens = g_max_tokens;
                    if (ImGui::SliderInt("##MaxTokens", &max_tokens, 100, 8192)) {
                        g_max_tokens = max_tokens;
                        SaveConfig();
                    }

                    ImGui::Spacing();

                    // 超时设置
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "超时时间 (毫秒):");
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    int timeout = g_timeout;
                    if (ImGui::SliderInt("##Timeout", &timeout, 5000, 60000)) {
                        g_timeout = timeout;
                        SaveConfig();
                    }

                    ImGui::EndTabItem();
                }

                // ============ 语境设置标签页 ============
                if (ImGui::BeginTabItem("语境设置")) {
                    ImGui::Spacing();

                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f),
                        "选择智能体模式，不同模式会调整工作窗口界面");
                    ImGui::Separator();
                    ImGui::Spacing();

                    // 模式选择
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "智能体模式:");

                    bool is_translation = (g_current_mode == AgentMode::Translation);
                    bool is_analysis = (g_current_mode == AgentMode::Analysis);
                    bool is_summary = (g_current_mode == AgentMode::Summary);
                    bool is_polish = (g_current_mode == AgentMode::Polish);
                    bool is_custom = (g_current_mode == AgentMode::Custom);

                    if (ImGui::RadioButton("翻译助手", is_translation)) {
                        SetAgentMode(AgentMode::Translation);
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("分析助手", is_analysis)) {
                        SetAgentMode(AgentMode::Analysis);
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("总结助手", is_summary)) {
                        SetAgentMode(AgentMode::Summary);
                    }

                    ImGui::Spacing();

                    if (ImGui::RadioButton("润色助手", is_polish)) {
                        SetAgentMode(AgentMode::Polish);
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("自定义", is_custom)) {
                        SetAgentMode(AgentMode::Custom);
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // 翻译模式专用设置
                    if (g_current_mode == AgentMode::Translation) {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "翻译设置:");

                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "目标语言:");
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                        if (ImGui::InputText("##TargetLanguage", g_target_language_buffer, sizeof(g_target_language_buffer))) {
                            g_target_language = g_target_language_buffer;
                            SaveConfig();
                        }
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                            "例如：简体中文、English、日本語、한국어等");

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }

                    // 提示词编辑区
                    if (g_current_mode == AgentMode::Custom) {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "自定义提示词 (自由编辑):");
                        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f),
                            "提示：可以使用 {target_language} 作为占位符");
                    }
                    else {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "当前提示词 (可编辑切换模式):");
                    }

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::InputTextMultiline("##SystemPrompt", g_system_prompt_buffer, sizeof(g_system_prompt_buffer),
                        ImVec2(-1, 150), ImGuiInputTextFlags_AllowTabInput)) {
                        g_system_prompt = g_system_prompt_buffer;
                        SaveConfig();

                        // 如果用户手动编辑了提示词，检查是否需要切换模式
                        if (g_current_mode != AgentMode::Custom) {
                            bool still_match = true;
                            switch (g_current_mode) {
                            case AgentMode::Translation:
                                if (g_system_prompt.find("翻译") == std::string::npos) still_match = false;
                                break;
                            case AgentMode::Analysis:
                                if (g_system_prompt.find("分析") == std::string::npos) still_match = false;
                                break;
                            case AgentMode::Summary:
                                if (g_system_prompt.find("总结") == std::string::npos) still_match = false;
                                break;
                            case AgentMode::Polish:
                                if (g_system_prompt.find("润色") == std::string::npos) still_match = false;
                                break;
                            default: break;
                            }
                            if (!still_match) {
                                g_current_mode = AgentMode::Custom;
                                g_mode_display_name = "处理";
                                g_show_original_text = false;
                            }
                        }
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // 提示词示例参考
                    if (ImGui::CollapsingHeader("提示词示例参考")) {
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                            "点击下方按钮可切换为对应模式（会覆盖当前提示词）:");

                        if (ImGui::Button("翻译模式", ImVec2(100, 25))) {
                            SetAgentMode(AgentMode::Translation);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("分析模式", ImVec2(100, 25))) {
                            SetAgentMode(AgentMode::Analysis);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("总结模式", ImVec2(100, 25))) {
                            SetAgentMode(AgentMode::Summary);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("润色模式", ImVec2(100, 25))) {
                            SetAgentMode(AgentMode::Polish);
                        }
                    }

                    ImGui::EndTabItem();
                }

                // ============ 界面设置标签页 ============
                if (ImGui::BeginTabItem("界面设置")) {
                    ImGui::Spacing();

                    // 快捷键说明
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "快捷键说明：");
                    ImGui::BulletText("F1 - 开启/关闭监听 [当前: %s]", g_monitoring ? "开启" : "关闭");
                    ImGui::BulletText("F2 - 显示/隐藏工作窗口");
                    ImGui::BulletText("F3 - 显示/隐藏设置窗口");
                    ImGui::BulletText("ESC - 关闭所有弹出窗口");
                    ImGui::BulletText("鼠标中键 - 处理选中文本");
                    ImGui::BulletText("Ctrl+鼠标滚轮 - 调整字体大小");
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // 字体大小控制
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "字体设置");
                    float current_scale = g_font_scale;
                    ImGui::Text("字体大小: %.1f%%", current_scale * 100.0f);
                    if (ImGui::SliderFloat("##FontScale", &current_scale, g_min_font_scale, g_max_font_scale, "%.2fx")) {
                        UpdateFontSize(current_scale);
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // 窗口透明度控制
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "窗口透明度");
                    float max_alpha = 0.98f;
                    if (ImGui::SliderFloat("##WindowAlpha", &g_window_alpha, 0.3f, max_alpha, "%.2f")) {
                        if (g_window_alpha > max_alpha) g_window_alpha = max_alpha;
                        if (g_window_alpha < 0.3f) g_window_alpha = 0.3f;
                        ImGuiTransparentWindow::SetAlpha(g_window_alpha);
                        SaveConfig();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // 监听状态控制
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "监听状态");
                    if (g_monitoring) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "● 监听中");
                    }
                    else {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "○ 监听已停止");
                    }
                    if (ImGui::Button(g_monitoring ? "停止监听 (F1)" : "开始监听 (F1)", ImVec2(200, 30))) {
                        ToggleMonitoring();
                    }

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::Spacing();
            ImGui::Separator();

            // 保存提示
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "配置文件: %s", GetConfigPath().c_str());
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "所有设置自动保存");

            ImGui::End();
            ImGuiTransparentWindow::PopWindowStyles();
        }

        // ============ 工作窗口 - 语化场 ============
        if (g_show_translation_window) {
            ImGui::SetNextWindowPos(ImVec2(
                (float)(g_mouse_pos.x + 50),
                (float)(g_mouse_pos.y + 50)
            ), ImGuiCond_Appearing);

            ImGui::SetNextWindowSize(ImVec2(550, 450), ImGuiCond_Appearing);

            ImGuiTransparentWindow::PushWindowStyles();

            // 动态窗口标题
            std::string window_title = "智语Core · 语化场 [ " + g_mode_display_name + "模式 ]";
            ImGui::Begin(window_title.c_str(), &g_show_translation_window,
                ImGuiWindowFlags_NoCollapse);

            // 原文区域（仅翻译模式显示）
            if (g_current_mode == AgentMode::Translation && g_show_original_text) {
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "原文内容：");
                ImGui::Separator();

                if (!g_clipboard_content.empty()) {
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
                    ImGui::TextWrapped("%s", g_clipboard_content.c_str());
                    ImGui::PopTextWrapPos();
                }
                else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "无内容");
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }

            // 结果区域（动态标签）
            std::string result_label;
            switch (g_current_mode) {
            case AgentMode::Translation:
                result_label = "翻译结果 [" + g_target_language + "]：";
                break;
            case AgentMode::Analysis:
                result_label = "分析结果：";
                break;
            case AgentMode::Summary:
                result_label = "总结结果：";
                break;
            case AgentMode::Polish:
                result_label = "润色结果：";
                break;
            default:
                result_label = "处理结果：";
                break;
            }

            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", result_label.c_str());
            ImGui::Separator();

            ImGui::BeginChild("WorkResult", ImVec2(0, 200), true);
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);

            if (g_translating) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s中，请稍候...", g_mode_display_name.c_str());

                static float progress = 0.0f;
                progress += ImGui::GetIO().DeltaTime * 2.0f;
                if (progress > 1.0f) progress = 0.0f;
                ImGui::ProgressBar(progress, ImVec2(-1.0f, 6.0f), "");
            }
            else if (g_translation_error) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::TextWrapped("✗ %s", g_error_message.c_str());
                ImGui::PopStyleColor();
            }
            else if (!g_translated_content.empty()) {
                ImGui::TextWrapped("%s", g_translated_content.c_str());
            }
            else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "等待%s...", g_mode_display_name.c_str());
            }

            ImGui::PopTextWrapPos();
            ImGui::EndChild();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 动态计算按钮数量
            int button_count = (g_current_mode == AgentMode::Translation) ? 4 : 3;
            float buttonWidth = (ImGui::GetContentRegionAvail().x - 20) / button_count;

            // 复制原文按钮（仅翻译模式显示）
            if (ImGui::Button("复制原文", ImVec2(buttonWidth, 35))) {
                if (!g_clipboard_content.empty()) {
                    CopyToClipboard(g_clipboard_content);  // 调用了一个可能有问题的函数
                }
            }

            // 复制结果按钮
            std::string copy_btn_text = "复制" + g_mode_display_name;
            if (ImGui::Button(copy_btn_text.c_str(), ImVec2(buttonWidth, 35))) {
                if (!g_translated_content.empty()) {
                    std::string content_to_copy;

                    if (g_translation_error) {
                        // 如果出错，只复制错误信息部分，去掉"失败: "前缀
                        content_to_copy = g_error_message;
                    }
                    else {
                        // 正常情况复制翻译结果
                        content_to_copy = g_translated_content;
                    }

                    if (!content_to_copy.empty()) {
                        CopyToClipboard(content_to_copy);
                    }
                }
            }

            ImGui::SameLine();

            // 重新处理按钮
            std::string reprocess_btn_text = "重新" + g_mode_display_name;
            if (ImGui::Button(reprocess_btn_text.c_str(), ImVec2(buttonWidth, 35))) {
                if (!g_clipboard_content.empty() && !g_translating) {
                    AsyncTranslate(g_clipboard_content);
                }
            }

            ImGui::SameLine();

            // 关闭按钮
            if (ImGui::Button("关闭 (ESC)", ImVec2(buttonWidth, 35))) {
                g_show_translation_window = false;
            }

            if (!g_translating && !g_translated_content.empty() && !g_translation_error) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s完成", g_mode_display_name.c_str());
            }

            ImGui::End();
            ImGuiTransparentWindow::PopWindowStyles();
        }

        if (io.KeyCtrl && io.MouseWheel != 0.0f) {
            float new_scale = g_font_scale + (io.MouseWheel > 0 ? 0.1f : -0.1f);
            UpdateFontSize(new_scale);
        }

        if (g_current_font != nullptr) {
            ImGui::PopFont();
        }

        ImGui::Render();
        ImGuiTransparentWindow::UpdateAllViewports();
        ImGuiTransparentWindow::SetAllViewportsTopMost();

        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    // Cleanup
    SaveConfig();

    if (g_keyboard_hook) {
        UnhookWindowsHookEx(g_keyboard_hook);
        g_keyboard_hook = nullptr;
    }

    if (g_mouse_hook) {
        UnhookWindowsHookEx(g_mouse_hook);
        g_mouse_hook = nullptr;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    IDXGIFactory* pSwapChainFactory;
    if (SUCCEEDED(g_pSwapChain->GetParent(IID_PPV_ARGS(&pSwapChainFactory)))) {
        pSwapChainFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
        pSwapChainFactory->Release();
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
