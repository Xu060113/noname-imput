# 语句拼音

语句拼音是一个接入 DeepSeek 的中文系统输入法。空格或数字选拼音，回车确认正在拼写的英文单词。先后确认 `hello` 和 `world`，结果为 `hello world`。英文空格在下一个词上屏时补入，句末没有多余空格。

开启 AI 后，每次确认文字并停顿约 600 毫秒，输入法会用本次连续输入的片段向 DeepSeek 请求建议。网络和模型生成需要额外时间。提问的第一个答案词另起一行，普通续写紧接原文。Tab 接受第一项，1、2、3 可选择相应候选。Esc 收起候选。继续打字会取消旧请求。

## macOS

支持 macOS 13 及以上的 Apple 芯片和 Intel Mac。双击「安装语句拼音.command」。安装后，在系统设置 → 键盘 → 输入法 → 编辑中确认「语句拼音」已在列表内。需要手动添加时，在「简体中文」中找到它。已经运行旧版本时，重新登录可载入新构建。

从菜单栏选择「语句拼音」，在同一菜单打开「DeepSeek 设置…」。填写 API Key，点「测试连接」可以看到首组候选耗时。保存后，密钥位于当前用户的 macOS 钥匙串。断网时仍可使用本地简体全拼。

源码构建需要 Xcode Command Line Tools。首次构建从 librime 官方下载校验过的 1.17.0 通用二进制包。运行 `Scripts/build.sh` 生成应用，运行 `Scripts/test.sh` 验证真实拼音和模拟分支。如果测试进程中已有 `DEEPSEEK_API_KEY` 环境变量，测试还会检查两种模式和各自的真实分支。

## Windows

将 `Yuju-Setup-Windows-x64.exe` 复制到 Windows 10 或 11 的 Intel/AMD 64 位电脑并运行。安装时需要管理员权限以注册系统输入法。安装后，从开始菜单打开「语句拼音 → DeepSeek 设置」，填写自己的 API Key 并保存。按 Win + 空格切换输入法。若列表中尚未出现，在「设置 → 时间和语言 → 语言和区域 → 中文（简体）→ 语言选项 → 添加键盘」中选择「语句拼音」。卸载入口在「已安装的应用」中。

Windows 版使用原生 TSF 组件，面向 x64 桌面应用。设置按用户保存，密钥存入 Windows 凭据管理器中的 `Yuju/DeepSeek`。本版安装程序未经 Authenticode 发布者签名。Windows ARM64 原生应用和 32 位应用需要对应架构的输入法组件。

## 在 Windows 上重新构建

1. 安装 Python 3、Zig 0.13.0 和 NSIS 3。如果命令不在 PATH，将环境变量 `ZIG` 和 `MAKENSIS` 分别设为可执行文件的绝对路径。
2. 在源码根目录运行 `py -3 Scripts/build_windows.py`。脚本验证 librime 官方归档的 SHA-256，在本机编译拼音词库并生成 TSF 组件、设置程序和安装器。
3. 安装器输出到 `dist/Yuju-Setup-Windows-x64.exe`。在记事本与浏览器多行输入框中验证新版本。具体场景见「Windows开发提示词.md」。

## 项目内容

`Sources` 为 Mac 实现，`Windows` 为 TSF、WinHTTP、设置和安装器源码，`Resources/Rime` 为共享方案和词典，`Tests` 为固定响应与在线检查。模型为 `deepseek-flash`，通过 `https://api.deepseek.com/chat/completions` 流式调用，关闭思考模式。英文空格、问答换行和各候选的实际文字均由输入法在确认后提交。

librime 来自 rime/librime 1.17.0。简体词典来自 rime/rime-pinyin-simp 的 0c6861ef7420ee780270ca6d993d18d4101049d0。Windows JSON 库使用 nlohmann/json 3.11.3。授权文本位于 `Resources/Licenses`。
