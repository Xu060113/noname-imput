import Foundation

enum WritingMode: String, Codable { case answer, continuation }
enum Frame: Equatable { case mode(WritingMode), words([String]), end }
enum PredictionError: LocalizedError {
    case format, incomplete, http(Int)
    var errorDescription: String? {
        switch self {
        case .format: return "模型返回的候选格式异常。"
        case .incomplete: return "回答流提前结束。"
        case .http(401), .http(403): return "API Key 无效或没有访问权限。"
        case .http(402): return "DeepSeek 账户余额不足。"
        case .http(429): return "请求过于频繁，请稍后再试。"
        case .http(let status): return "DeepSeek 请求失败（HTTP \(status)）。"
        }
    }
}

// The API streams arbitrary pieces of text. Only a complete JSON line is a candidate row.
struct FrameDecoder {
    private var buffer = ""
    private var modeSeen = false
    private(set) var ended = false

    mutating func feed(_ fragment: String) throws -> [Frame] {
        buffer += fragment
        guard buffer.utf8.count <= 65536 else { throw PredictionError.format }
        var frames: [Frame] = []
        while let newline = buffer.firstIndex(of: "\n") {
            let line = String(buffer[..<newline]).trimmingCharacters(in: .whitespacesAndNewlines)
            buffer.removeSubrange(...newline)
            if line.isEmpty { continue }
            guard !ended else { throw PredictionError.format }
            let data = Data(line.utf8)
            if !modeSeen {
                guard let head = try? JSONDecoder().decode([String:WritingMode].self, from: data),
                      let mode = head["mode"] else { throw PredictionError.format }
                modeSeen = true
                frames.append(.mode(mode))
            } else if let words = try? JSONDecoder().decode([String].self, from: data) {
                var unique: [String] = []
                for word in words where !unique.contains(word) {
                    guard word.count <= 16, !word.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
                          word.rangeOfCharacter(from: .newlines.union(.controlCharacters)) == nil else {
                        throw PredictionError.format
                    }
                    unique.append(word)
                }
                guard (1...3).contains(unique.count) else { throw PredictionError.format }
                frames.append(.words(unique))
            } else if (try? JSONDecoder().decode([String:Bool].self, from: data))?["end"] == true {
                ended = true
                frames.append(.end)
            } else {
                throw PredictionError.format
            }
        }
        return frames
    }
}

struct PredictionInput: Encodable {
    let input: String
    var accepted = ""
    var mode: WritingMode?
}

enum EnglishSpacing {
    static func join(_ word: String, after previous: String) -> String {
        guard let last = previous.last, !word.isEmpty else { return word }
        let trimmed = String(word.drop(while: { $0 == " " }))
        guard let first = trimmed.first else { return word }
        let letter: (Character) -> Bool = { $0.isASCII && $0.isLetter }
        if ",.!?;:)]’'".contains(first) { return trimmed }
        if last.isWhitespace { return trimmed }
        let afterPunctuation = ",.!?;:".contains(last) && previous.dropLast().last.map(letter) == true
        if letter(first) && (letter(last) || afterPunctuation) { return " " + trimmed }
        return word
    }
}

struct WordChoice {
    let text: String
    let branch: Bool
}

// A generation belongs to a single caret and branch. Late frames never reach a newer buffer.
struct PredictionBuffer {
    private(set) var generation = UUID()
    private(set) var input: PredictionInput?
    private var rows: [[String]] = []
    private(set) var ended = false
    var choices: [String] { rows.first ?? [] }
    var isWaiting: Bool { input != nil && !ended && rows.isEmpty }

    mutating func reset(_ next: PredictionInput? = nil) {
        generation = UUID()
        input = next
        rows.removeAll()
        ended = false
    }

    mutating func receive(_ frame: Frame, generation id: UUID) {
        guard id == generation, input != nil, !ended else { return }
        switch frame {
        case .mode(let mode): if input?.mode == nil { input?.mode = mode }
        case .words(let words): rows.append(words)
        case .end: ended = true
        }
    }

    mutating func choose(_ index: Int) -> WordChoice? {
        guard var current = input, choices.indices.contains(index) else { return nil }
        let selected = rows.removeFirst()[index]
        let prefix = current.mode == .answer && current.accepted.isEmpty ? "\n" : ""
        let preceding = current.accepted.isEmpty ? (current.mode == .answer ? "" : current.input) : current.accepted
        let word = EnglishSpacing.join(selected, after: preceding)
        current.accepted += word
        if index == 0 { input = current } else { reset(current) }
        return WordChoice(text: prefix + word, branch: index != 0)
    }
}
