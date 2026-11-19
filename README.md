# 语句拼音

语句拼音是一款中文系统输入法。打字用本地简体全拼，可以离线使用；接入自己的 DeepSeek API Key 后，输入法会在光标旁逐词续写或回答问题。Windows 版另外支持**文件问答**：用英文提问时，输入法依据你指定文件夹里的 PDF 回答，并附上出处。

## 版本状态

| 平台 | 状态 |
| --- | --- |
| Windows 10／11 x64 | 本仓库当前版本 = v1.0 + 文件问答，已在记事本和 Edge 中完成验收（见 `docs/文件问答实施方案.md`）。尚未发布到 GitHub Release，需要按下文在本地构建 |
| macOS | 停留在 v1.0，不再维护，没有文件问答 |

GitHub 上的 [v1.0.0 Release](https://github.com/Xu060113/noname-imput/releases/tag/v1.0.0) 提供 v1.0 的 Mac 安装包、Windows 安装程序和源码包，不含文件问答。

## 怎么用

切换到「语句拼音」（Windows 按 Win + 空格）。拼音候选用空格或数字确认。英文用回车确认当前单词，例如依次输入 `hello`、回车、`world`、回车，结果为 `hello world`。

开启 AI 后：

| 你做了什么 | 输入法怎么做 | 是否带上课程文件 |
| --- | --- | --- |
| 打完一句**中文**，停顿约 0.6 秒 | 续写半句，或回答问题（如「什么是大模型」），候选 1～3 个 | 不带 |
| 打完一句**英文**，以 `?` 结尾，停顿约 0.6 秒（Windows） | 依据文件用英文回答，末尾附 `(Source: 文件名, p.页码)`；文件里没有时只回 `Not found in your course files.` | 带 |
| 打英文但**没有**问号 | 不请求 | — |
| **粘贴**一段英文（Ctrl+V，不超过 2000 字符），停顿约 0.6 秒（Windows） | 把粘贴的整段当作问题，依据文件回答 | 带 |
| 粘贴中文，或粘贴超过 2000 字符 | 不请求 | — |

- 按 **Tab** 接受第一个候选，连续按 Tab 一个词一个词接下去。回答紧接在问题后面、空一格，不另起一行。
- 中文续写时，按 **2** 或 **3** 选备选说法，后面的内容沿着选中的词重新生成；文件问答每行只有一个候选。
- 按 **Esc** 收起候选；移动光标、切换窗口也会收起，不会把字插到别处。密码框里不预测。
- 英文句子里打的标点是半角（`?` 而不是「？」）。

## Windows：安装与设置

1. 在本地构建（见下文）后运行 `dist/Yuju-Setup-Windows-x64.exe`。安装需要管理员权限以注册系统输入法；安装程序未签名，SmartScreen 拦截时选「更多信息 → 仍要运行」。
2. 按 Win + 空格切换输入法。若列表中没有「语句拼音」，在「设置 → 时间和语言 → 语言和区域 → 中文（简体）→ 语言选项 → 添加键盘」中添加。
3. 从开始菜单打开「语句拼音 → DeepSeek 设置」，填写自己的 API Key。密钥存入 Windows 凭据管理器的 `Yuju/DeepSeek`，设置按用户保存在 `HKCU\Software\Yuju`。
4. 卸载入口在「已安装的应用」中。卸载不会删除密钥、设置和知识文件。

升级时，正在使用旧版组件的程序要重启后才会加载新版。

## Windows：文件问答

### 准备知识文件

文件问答读取一个由 PDF 生成的知识文件。在**自己打开的** PowerShell 里，于仓库根目录运行（需要 [uv](https://docs.astral.sh/uv/)，它会临时下载 PDF 库 `pymupdf`）：

```powershell
uv run --with pymupdf python -X utf8 Scripts/build_knowledge.py --set-folders "D:\课程\课件" "D:\课程\习题"
```

- 文件夹清单保存在 `%LOCALAPPDATA%\Yuju\knowledge_folders.txt`（一行一个，可用记事本编辑），知识文件和抽取报告也在这个目录。
- 文件有增删改后，不带参数重跑一次 `Scripts/build_knowledge.py` 即可，不用重装输入法；输入法发现知识文件变了会自动重新读取。
- 只处理 PDF（含子文件夹）。扫描版或纯图片的页面抽不到文字，抽取报告会列出这些页。总文字量上限约 300 万字符。
- 不要在 Claude 桌面端等会重定向 AppData 的环境里运行，否则文件可能写不到输入法读得到的位置。

### 发送了什么

文件问答时，请求开头是知识文件全文（同一份内容每次都一样，DeepSeek 的上下文缓存可以命中，命中后每次输入费用约为未命中的 1/50），接着是回答规则，最后是你的问题。问题只取光标所在的那一句英文，或你粘贴的那一段。中文续写不带文件，发送的是本次连续输入的文字和已接受的前缀。输入法不写日志，也不连接 `api.deepseek.com` 以外的地址。

回答规则在 `Windows/file_rules.txt`，安装时复制到程序目录。改规则后需要重新构建并安装。

### 已知限制

- 微信里不出候选框，暂不支持。
- Edge 中只要插入内容以换行开头，第一个词就会被重复插入，原因未查清；因此回答不再另起一行。
- 只用于 x64 桌面应用；Windows 商店类受限程序可能读不到知识文件。

## Windows：构建

1. 安装 Python 3、[Zig 0.13.0](https://pypi.org/project/ziglang/0.13.0/)（构建脚本只接受这个版本）和 NSIS 3。命令不在 PATH 时，把环境变量 `ZIG`、`MAKENSIS` 设为可执行文件的绝对路径。
2. 在仓库根目录运行：

   ```powershell
   py -3 -X utf8 Scripts/build_windows.py
   ```

   脚本校验并解压 librime 官方归档，编译并运行 `Windows/common_test.cpp`（文字处理函数的测试，失败则停止），然后编译 TSF 组件、设置程序和拼音词库，最后用 NSIS 生成安装程序。中间文件在 `.build/`，安装程序在 `dist/Yuju-Setup-Windows-x64.exe`。

## 开发与测试

- `Scripts/test_knowledge_latency.py`：按输入法的文件模式格式向 DeepSeek 发测试问题，测首个候选耗时、缓存命中，并自动判定答案（出处是否存在、是否照抄原文、找不到时是否只回那一句）。`--dry-run` 只做离线自检，不联网。联网测试需在自己的终端里设置 `DEEPSEEK_API_KEY`，8 个问题约花费 $0.005（缓存命中时）。问题针对你自己的文件：把 `Scripts/test_questions.example.json` 复制为 `Scripts/test_questions.local.json` 后填写（该文件不提交）。结果写入 `.build/phase0_latency_report.json`，其中含文件原文，也不提交。
- `Scripts/windows_dev_reinstall.ps1`：开发时不重启电脑就替换已安装的版本（以管理员身份运行）。被占用的旧文件先改名移到 `C:\Program Files\YujuInput\old\`。
- `Scripts/windows_cleanup_old.ps1`：删除上面换下的旧文件，仍被占用的安排在下次重启时删除（以管理员身份运行）。
- `Tests/windows_ime_test_page.html`：浏览器测试页，含多行输入框和密码框。
- 验收用例 T1～T12、每轮问题与修复记录见 `docs/文件问答实施方案.md`。

## macOS（v1.0，不再维护）

解压 Mac 安装包，双击「安装语句拼音.command」，在系统设置 → 键盘 → 输入法中确认「语句拼音」已在列表内。从菜单栏「语句拼音 → DeepSeek 设置…」填写 API Key，密钥保存在钥匙串。源码构建需要 Xcode Command Line Tools：`Scripts/build.sh` 生成应用，`Scripts/test.sh` 运行测试。Mac 版没有同步 Windows 版之后的任何改动。

## 项目内容

| 路径 | 内容 |
| --- | --- |
| `Windows/` | TSF 输入法组件（`ime.cpp`）、DeepSeek 流式请求与文件模式（`model.hpp`）、文字处理函数（`common.hpp`）及其测试、设置程序、NSIS 安装脚本、文件问答规则 `file_rules.txt` |
| `Scripts/` | Windows 构建、知识文件预处理、测速与答案检查、开发用重装与清理脚本；Mac 构建与测试脚本 |
| `Resources/Rime/` | 两端共用的拼音方案和词典 |
| `Sources/`、`Tests/Checks.swift`、`安装语句拼音.command` | Mac 版（不再维护） |
| `Tests/windows_ime_test_page.html` | Windows 浏览器测试页 |
| `docs/` | 文件问答的实施方案与验收记录 |

模型为 `deepseek-flash`，通过 `https://api.deepseek.com/chat/completions` 流式调用，关闭思考模式。librime 来自 rime/librime 1.17.0；简体词典来自 rime/rime-pinyin-simp 的 0c6861ef7420ee780270ca6d993d18d4101049d0；Windows JSON 库使用 nlohmann/json 3.11.3。授权文本位于 `Resources/Licenses`。
