# 智语Core (ZhiYuCore)

**一个基于DeepSeek API的智能桌面助手，支持翻译、分析、总结、润色等多种智能体模式**

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-blue)](https://www.microsoft.com/windows)
[![C++](https://img.shields.io/badge/C++-20-blue.svg)](https://isocpp.org/)

## 📖 项目简介

智语Core是一个使用C++和Dear ImGui开发的Windows桌面应用，通过全局鼠标/键盘钩子实现文本快速处理。用户只需选中文本并点击鼠标中键，即可自动调用DeepSeek API进行处理，处理结果实时显示在透明浮动窗口中。
### 视频演示地址：
【智语Core】 https://www.bilibili.com/video/BV1gyMx6mEvP/?share_source=copy_web&vd_source=8f734f1e86c1b345adc07138e11bb4e1
> **🤖 AI生成声明**：本项目所有代码均由DeepSeek生成及Claude纠正已知bug，作者仅负责需求分析、设计思想、部署测试和反馈Bug。

### 核心特性

- **🎯 多种智能体模式**：翻译、分析、总结、润色、自定义
- **🖱️ 一键触发**：选中文本后点击鼠标中键，自动复制并处理
- **💫 透明浮动窗口**：处理结果以半透明窗口形式显示在鼠标位置
- **⚙️ 全局热键**：F1监听开关、F2显示工作窗口、F3打开设置、ESC关闭窗口
- **🔧 完全可配置**：支持自定义API参数、系统提示词、字体大小、窗口透明度
- **💾 自动保存**：所有配置自动保存到本地JSON文件

## 📸 界面预览

| 工作窗口(语化场) | 设置窗口 |
|:---:|:---:|
| 显示原文和处理结果，支持一键复制 | API配置、模式选择、界面设置 |

## 🚀 快速开始

### 环境要求

- Windows 10/11 (x64)
- [Visual Studio 2022](https://visualstudio.microsoft.com/) (v145工具集)
- DeepSeek API Key

### 编译步骤

1. **克隆仓库**
```bash
git clone https://github.com/yourname/ZhiYuCore.git
cd ZhiYuCore
```

2. **使用Visual Studio打开** `ZhiYuCore.slnx`

3. **配置项目**
   - 确保包含目录指向`include`文件夹
   - 项目已配置C++20标准

4. **编译运行**
   - 选择`Release x64`配置
   - 按F5编译运行

### 首次使用

1. 启动后会显示系统托盘通知
2. 按`F3`打开设置窗口
3. 填入DeepSeek API Key
4. 选择需要的智能体模式
5. 按`F1`开启监听，选中任意文本，点击鼠标中键即可

## ⌨️ 快捷键说明

| 快捷键 | 功能 |
|:---:|:---|
| `F1` | 开启/关闭监听 |
| `F2` | 显示/隐藏工作窗口 |
| `F3` | 显示/隐藏设置窗口 |
| `ESC` | 关闭所有弹出窗口 |
| `鼠标中键` | 处理选中的文本 |
| `Ctrl + 鼠标滚轮` | 调整工作窗口字体大小 |

## 🎭 智能体模式

### 翻译助手
将选中文本翻译为目标语言，同时显示原文和译文

### 分析助手
分析文本的核心主题、情感倾向和关键要点

### 总结助手
用3-5个要点概括文本核心内容

### 润色助手
优化文本表达方式，修正语法错误

### 自定义模式
完全自定义系统提示词，满足任意处理需求

## 🛠️ 技术架构

### 核心技术栈

| 组件 | 技术 |
|:---|:---|
| UI框架 | Dear ImGui 1.91+ |
| 图形后端 | DirectX 11 |
| HTTP客户端 | WinHTTP |
| JSON解析 | nlohmann/json |
| 全局钩子 | Windows Hooks (WH_MOUSE_LL, WH_KEYBOARD_LL) |

### 项目结构

```
ZhiYuCore/
├── ZhiYuCore.cpp              # 主程序入口
├── ZhiYuCore.vcxproj          # VS项目配置
├── include/
│   ├── imgui/                 # Dear ImGui库
│   │   ├── imgui_set_alpha.*  # 透明窗口扩展
│   │   └── ...
│   └── nlohmann/              # JSON库
│       └── json.hpp
└── zhiyu_config.json          # 配置文件(自动生成)
```

### 核心工作流程

```
1. 用户选中文本 → 鼠标中键
2. 模拟Ctrl+C → 读取剪贴板
3. 调用DeepSeek API → 异步处理
4. 显示浮动窗口 → 展示结果
5. 支持复制/重新处理
```

## ⚙️ 配置说明

配置文件`zhiyu_config.json`位于程序目录：

```json
{
    "api_key": "your-deepseek-api-key",
    "api_url": "https://api.deepseek.com",
    "model": "deepseek-chat",
    "temperature": 0.3,
    "max_tokens": 4096,
    "font_scale": 1.0,
    "window_alpha": 0.85,
    "target_language": "简体中文",
    "system_prompt": "自定义提示词...",
    "agent_mode": 0
}
```

## 🤝 贡献

由于本项目代码由AI生成，目前不接受代码PR。欢迎通过Issue提交：

- 🐛 Bug反馈
- 💡 功能建议
- 📖 文档改进

## 📄 许可证

本项目采用MIT许可证，详见[LICENSE](LICENSE)文件。

## 🙏 致谢

- [DeepSeek](https://deepseek.com) - 提供强大的API服务
- [Claude](https://claude.ai/) - 提供的代码生成服务
- [Dear ImGui](https://github.com/ocornut/imgui) - 优秀的即时模式GUI库
- [nlohmann/json](https://github.com/nlohmann/json) - 便捷的JSON解析库

---

**智语Core - 让AI触手可及**
