# 语句拼音

语句拼音是一款支持 macOS 和 Windows 的中文系统输入法。你可以用本地简体全拼打字，也可以接入自己的 DeepSeek API Key，让输入法在光标旁逐词回答问题或续写半句话。

## 下载

| 平台 | 适用系统 | 下载 |
| --- | --- | --- |
| macOS | macOS 13 及以上，Apple 芯片或 Intel | [Mac 安装包（ZIP）](https://github.com/Xu060113/noname-imput/releases/download/v1.0.0/Yuju-macOS-universal.zip) |
| Windows | Windows 10 或 11，Intel／AMD x64 桌面应用 | [Windows 安装程序（EXE）](https://github.com/Xu060113/noname-imput/releases/download/v1.1.2/Yuju-Setup-Windows-x64.exe) |

需要自行构建时，可以下载包含两端代码、词典和测试的[完整源码包](https://github.com/Xu060113/noname-imput/releases/download/v1.1.2/Yuju-Source.zip)。

## 试着输入一句话

1. 切换到「语句拼音」，打出「什么是大模型」，用空格确认拼音。开启 AI 后，停顿约 600 毫秒会触发请求，候选的出现时间还取决于网络和模型响应。
2. 光标旁可能出现 `1 大模型　2 它　3 通常`。按 **Tab** 接受「大模型」，答案另起一行。再按 Tab 可以依次接上「是」「一种」等词，直到回答结束。按 **2** 或 **3** 会沿你选中的措辞继续生成。
3. 输入「我今天想」时，所选词直接接在原句后。按 **Esc** 收起候选，也可以继续打字。

拼音候选用空格或数字确认。输入英文时用回车确认当前单词，例如依次输入 `hello`、回车、`world`、回车，结果为 `hello world`。

本地拼音可以离线使用。启用 AI 预测后，本次连续输入的片段及已接受的续写前缀会发送给 DeepSeek。请在下方对应平台的设置窗口填写自己的密钥。

## macOS

解压 Mac 安装包，打开其中的文件夹，双击「安装语句拼音.command」。安装后，在系统设置 → 键盘 → 输入法 → 编辑中确认「语句拼音」已在列表内。需要手动添加时，在「简体中文」中找到它。已经运行旧版本时，重新登录可载入新构建。

从菜单栏选择「语句拼音」，在同一菜单打开「DeepSeek 设置…」。填写 API Key，点「测试连接」可以看到首组候选耗时。保存后，密钥位于当前用户的 macOS 钥匙串。断网时仍可使用本地简体全拼。

源码构建需要 Xcode Command Line Tools。首次构建从 librime 官方下载校验过的 1.17.0 通用二进制包。运行 `Scripts/build.sh` 生成应用，运行 `Scripts/test.sh` 验证真实拼音和模拟分支。如果测试进程中已有 `DEEPSEEK_API_KEY` 环境变量，测试还会检查两种模式和各自的真实分支。

## Windows

将 `Yuju-Setup-Windows-x64.exe` 复制到 Windows 10 或 11 的 Intel/AMD 64 位电脑并运行。安装时需要管理员权限以注册系统输入法。安装后，从开始菜单打开「语句拼音 → 控制后台」，填写自己的 API Key，勾选「停顿后显示 AI 候选」并保存。按 Win + 空格切换输入法。若列表中尚未出现，在「设置 → 时间和语言 → 语言和区域 → 中文（简体）→ 语言选项 → 添加键盘」中选择「语句拼音」。卸载入口在「已安装的应用」中。

从旧版本升级后，请重新打开原先正在使用语句拼音的编辑器，以载入新版输入法。

### 调整回答和续写长度

「控制后台」可以随时从开始菜单打开。回答「什么是大模型」和接着写「我今天想」使用各自的字符上限。

| 后台设置 | 默认上限 | 可调范围 |
| --- | --- | --- |
| 问题回答上限 | 80 个字符 | 8–300 |
| 语句续写上限 | 24 个字符 | 8–300 |

例如，将回答设为 120、续写设为 16，提问时可以逐词接出较长解释，写半句话时最多补 16 个字符。保存后从下一次预测生效，可以关闭后台继续打字。空格与标点计入上限，原文和答案开头的换行不计。改选其他候选时，已经接受的内容仍计入本次总长度。模型可以提前结束。

Windows 版使用原生 TSF 组件，面向 x64 桌面应用。设置按用户保存，密钥存入 Windows 凭据管理器中的 `Yuju/DeepSeek`。本版安装程序未经 Authenticode 发布者签名。Windows ARM64 原生应用和 32 位应用需要对应架构的输入法组件。

## 在 Windows 上重新构建

1. 安装 Python 3、Zig 0.13.0 和 NSIS 3。如果命令不在 PATH，将环境变量 `ZIG` 和 `MAKENSIS` 分别设为可执行文件的绝对路径。
2. 在源码根目录运行 `py -3 Scripts/build_windows.py`。脚本验证 librime 官方归档的 SHA-256，在本机编译拼音词库并生成 TSF 组件、设置程序和安装器。
3. 安装器输出到 `dist/Yuju-Setup-Windows-x64.exe`。在记事本与浏览器多行输入框中验证新版本。具体场景见「Windows开发提示词.md」。

## 项目内容

v1.0.0 已在 Mac 上验证真实拼音词库、英文空格与模拟流式回答。Windows v1.1.2 的回归测试覆盖问答换行后的光标检查、重复候选、备选分支、回答与续写长度和英文空格计数。`Tests/WindowsCandidates.cpp` 在临时 Windows RichEdit 窗口中通过 TSF 驱动已安装的输入法，使用控制后台配置的 DeepSeek 服务。默认样例为「什么是数据库」，传入 `greeting` 可测试「你好」上屏后停顿再按数字选词。

`Sources` 为 Mac 实现，`Windows` 为 TSF、WinHTTP、设置和安装器源码，`Resources/Rime` 为共享方案和词典，`Tests` 为固定响应与在线检查。模型为 `deepseek-flash`，通过 `https://api.deepseek.com/chat/completions` 流式调用，关闭思考模式。英文空格、问答换行和各候选的实际文字均由输入法在确认后提交。

librime 来自 rime/librime 1.17.0。简体词典来自 rime/rime-pinyin-simp 的 0c6861ef7420ee780270ca6d993d18d4101049d0。Windows JSON 库使用 nlohmann/json 3.11.3。授权文本位于 `Resources/Licenses`。
