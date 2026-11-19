import Foundation

struct CheckFailure: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

@main struct Checks {
    static func check(_ value: Bool, _ message: String) throws {
        if !value { throw CheckFailure(message: message) }
    }

    static func engine(_ dataPath: String, _ temp: String) throws {
        try check(PinyinSession.start(withData: dataPath, user: temp), "Rime 初始化")
        guard let session = PinyinSession.openEngine() else { throw CheckFailure(message: "无法建立拼音会话") }
        func type(_ text: String) {
            for key in text.utf8 { _ = session.press(Int32(key)) }
        }
        for (pinyin, word) in [("shenme","什么"), ("shi","是"), ("damoxing","大模型"), ("wo","我"), ("jintian","今天"), ("xiang","想")] {
            type(pinyin)
            guard let index = session.choices.firstIndex(of: word) else {
                throw CheckFailure(message: "\(pinyin) 候选错误 \(session.choices)")
            }
            _ = session.press(index == 0 ? 32 : Int32(49 + index))
            try check(session.takeCommit() == word && session.marked.isEmpty, "上屏 \(word)")
        }
        var englishText = ""
        for word in ["hello", "world"] {
            type(word)
            try check(session.rawInput == word, "英文原文缓冲")
            englishText += EnglishSpacing.join(session.rawInput, after: englishText)
            session.clear()
        }
        try check(englishText == "hello world", "按回车确认两次英文词的上屏内容")
        type("ni")
        _ = session.press(0xff08)
        try check(!session.marked.isEmpty, "退格保留剩余拼音")
        _ = session.press(0xff1b)
        try check(session.marked.isEmpty && session.takeCommit().isEmpty, "Esc 取消组合")
        print("✓ Rime 真实词库、空格及数字选词、退格与取消")
    }

    static func predictions() throws {
        let lines = #"{"mode":"answer"}"# + "\n" + #"["大模型","它","通常"]"# + "\n" +
            #"["是","可以"]"# + "\n" + #"["一种"]"# + "\n" + #"["模型"]"# + "\n" +
            #"["。"]"# + "\n" + #"{"end":true}"# + "\n"
        var parser = FrameDecoder()
        var frames = [Frame]()
        for ch in lines {
            let payload: [String:Any] = ["choices":[["delta":["content":String(ch)]]]]
            let json = String(decoding: try JSONSerialization.data(withJSONObject: payload), as: UTF8.self)
            if let text = try DeepSeek.fragment(from: "data: " + json) { frames += try parser.feed(text) }
        }
        try check(parser.ended, "分片解码")
        func seed() -> PredictionBuffer {
            var state = PredictionBuffer()
            state.reset(PredictionInput(input: "什么是大模型"))
            for frame in frames { state.receive(frame, generation: state.generation) }
            return state
        }
        var first = seed()
        var text = "什么是大模型"
        while let choice = first.choose(0) { text += choice.text }
        try check(text == "什么是大模型\n大模型是一种模型。" && first.ended && !first.isWaiting, "逐词选完后释放 Tab")
        for index in 1...2 {
            var branch = seed()
            let original = branch.generation
            let pick = branch.choose(index)
            try check(pick?.branch == true && pick?.text == (index == 1 ? "\n它" : "\n通常"), "分支选择")
            branch.receive(.words(["旧词"]), generation: original)
            try check(branch.choices.isEmpty && branch.isWaiting && branch.input?.mode == .answer, "过期帧失效")
            let next = branch.generation
            branch.receive(.words(["可以","是"]), generation: next)
            try check(branch.choose(0)?.text == "可以", "分支重复换行")
            branch.reset()
            branch.receive(.words(["迟到"]), generation: next)
            try check(branch.choose(0) == nil, "编辑或移位后取消")
        }
        var continuation = PredictionBuffer()
        continuation.reset(PredictionInput(input: "我今天想"))
        continuation.receive(.mode(.continuation), generation: continuation.generation)
        continuation.receive(.words(["去","试试","把"]), generation: continuation.generation)
        try check(continuation.choose(1)?.text == "试试", "续写应紧接原文")
        try check(EnglishSpacing.join("world", after: "hello") == " world", "英文单词空格")
        try check(EnglishSpacing.join(" world", after: "hello ") == "world", "避免双空格")
        try check(EnglishSpacing.join(" .", after: "hello") == ".", "标点前不加空格")
        try check(EnglishSpacing.join("模型", after: "AI") == "模型", "中文保留原样")
        var english = PredictionBuffer()
        english.reset(PredictionInput(input: "Hello"))
        english.receive(.mode(.continuation), generation: english.generation)
        english.receive(.words(["there","world"]), generation: english.generation)
        try check(english.choose(1)?.text == " world" && english.input?.accepted == " world", "英文分支上下文含空格")
        var invalid = FrameDecoder()
        do {
            _ = try invalid.feed("{\"mode\":\"answer\"}\n[\"危险\\n换行\"]\n")
            throw CheckFailure(message: "畸形候选被接受")
        } catch PredictionError.format {}
        print("✓ 流式拆包、问答换行、半句续写、第二和第三候选、迟到响应及结束")
    }

    @MainActor static func live(_ key: String) async throws {
        for (text, mode) in [("什么是大模型", WritingMode.answer), ("我今天想", .continuation)] {
            var input = PredictionInput(input: text)
            for branch in [false, true] {
                let start = Date()
                var first: TimeInterval?
                var prefix = ""
                var result = ""
                var actual: WritingMode?
                try await DeepSeek.stream(input, key: key) { frame in
                    switch frame {
                    case .mode(let m): actual = m
                    case .words(let words):
                        if first == nil {
                            first = Date().timeIntervalSince(start)
                            if words.count > 1 { prefix = words[1] }
                        }
                        result += words[0]
                    case .end: break
                    }
                }
                try check(actual == mode && first != nil && !result.isEmpty, "真实 API 模式或候选错误")
                print(String(format: "✓ %@%@ 首组 %.2f 秒　%@", text, branch ? "（第二选项后）" : "", first!, result))
                if !branch {
                    try check(!prefix.isEmpty, "缺少备选分支")
                    input.accepted = prefix
                    input.mode = mode
                }
            }
        }
    }

    static func main() async {
        do {
            try engine(CommandLine.arguments[1], CommandLine.arguments[2])
            try predictions()
            if let key = ProcessInfo.processInfo.environment["DEEPSEEK_API_KEY"], !key.isEmpty {
                try await live(key)
            } else { print("未配置 DEEPSEEK_API_KEY，已跳过真实 API。") }
        } catch { fputs("失败：\(error.localizedDescription)\n", stderr); exit(1) }
    }
}
