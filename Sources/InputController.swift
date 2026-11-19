import Cocoa
import Carbon
import InputMethodKit

@MainActor
@objc(YujuInputController)
final class InputController: IMKInputController {
    private let pinyin = PinyinSession.openEngine()
    private lazy var panel = CandidatePanel()
    private weak var editor: IMKTextInput?
    private var flow = PredictionBuffer()
    private var job: Task<Void, Never>?
    private var context = ""
    private var caret: Int?
    private var compositionStart: Int?
    private var hasMarkedText = false
    private let insertion = NSRange(location: NSNotFound, length: NSNotFound)

    override func activateServer(_ sender: Any!) {
        MainActor.assumeIsolated {
            super.activateServer(sender)
            editor = sender as? IMKTextInput
            resetContext()
            NotificationCenter.default.removeObserver(self, name: Preferences.changed, object: nil)
            NotificationCenter.default.addObserver(self, selector: #selector(resetContext), name: Preferences.changed, object: nil)
        }
    }

    override func recognizedEvents(_ sender: Any!) -> Int {
        Int(NSEvent.EventTypeMask.keyDown.union(.leftMouseDown).union(.rightMouseDown).rawValue)
    }

    @objc private func resetContext() {
        cancelPrediction()
        context = ""
        caret = nil
    }

    private func cancelPrediction() {
        job?.cancel()
        job = nil
        flow.reset()
        panel.hide()
    }

    private func matchesCaret(_ client: IMKTextInput) -> Bool {
        guard !IsSecureEventInputEnabled(), let caret, !context.isEmpty,
              client.selectedRange() == NSRange(location: caret, length: 0) else { return false }
        // Only check text this session inserted, never read the rest of a document.
        let tail = String(context.suffix(16))
        let count = tail.utf16.count
        guard caret >= count else { return false }
        return client.attributedSubstring(from: NSRange(location: caret - count, length: count))?.string == tail
    }

    private func schedule(_ input: PredictionInput, delayed: Bool) {
        job?.cancel()
        if delayed { flow.reset(input) }
        let id = flow.generation
        job = Task { @MainActor [weak self] in
            do {
                if delayed { try await Task.sleep(nanoseconds: 600_000_000) }
                try Task.checkCancellation()
                guard let self, self.flow.generation == id, let client = self.editor,
                      Preferences.enabled, self.matchesCaret(client) else { return }
                let key = try Preferences.key()
                guard !key.isEmpty else { self.cancelPrediction(); return }
                try await DeepSeek.stream(input, key: key) { [weak self] frame in
                    try Task.checkCancellation()
                    guard let self, self.flow.generation == id, let client = self.editor else { throw CancellationError() }
                    // Validate at each visible transition and immediately before any insertion.
                    let old = self.flow.choices
                    self.flow.receive(frame, generation: id)
                    if old != self.flow.choices {
                        guard self.matchesCaret(client) else { self.resetContext(); throw CancellationError() }
                        self.panel.show(self.flow.choices, ai: true, client: client)
                    }
                }
                if self.flow.generation == id { Preferences.lastIssue = nil }
            } catch {
                guard !Task.isCancelled, let self, self.flow.generation == id else { return }
                Preferences.lastIssue = error.localizedDescription
                self.cancelPrediction()
            }
        }
    }

    private func accept(_ index: Int, client: IMKTextInput) -> Bool {
        guard matchesCaret(client), let choice = flow.choose(index) else { resetContext(); return false }
        insert(choice.text, at: caret, in: client)
        panel.show(flow.choices, ai: true, client: client)
        if choice.branch, let input = flow.input { schedule(input, delayed: false) }
        return true
    }

    private func insert(_ text: String, at start: Int?, in client: IMKTextInput) {
        client.insertText(text, replacementRange: insertion)
        hasMarkedText = false
        guard let start, start != NSNotFound else { resetContext(); return }
        caret = start + text.utf16.count
        context = String((context + text).suffix(512))
    }

    private func updatePinyin(_ client: IMKTextInput, start: Int?) {
        guard let pinyin else { return }
        let commit = pinyin.takeCommit()
        if !commit.isEmpty { insert(commit, at: compositionStart ?? start, in: client) }
        let marked = pinyin.marked
        if !marked.isEmpty {
            if compositionStart == nil || !commit.isEmpty { compositionStart = !commit.isEmpty ? caret : start }
            client.setMarkedText(marked, selectionRange: NSRange(location: min(Int(pinyin.caret), marked.utf16.count), length: 0),
                                 replacementRange: insertion)
            hasMarkedText = true
            panel.show(pinyin.choices, highlight: pinyin.highlighted, ai: false, client: client)
        } else {
            if hasMarkedText { client.setMarkedText("", selectionRange: NSRange(location: 0, length: 0), replacementRange: insertion) }
            hasMarkedText = false
            compositionStart = nil
            panel.hide()
            if !commit.isEmpty, Preferences.enabled, caret != nil {
                schedule(PredictionInput(input: context), delayed: true)
            }
        }
    }

    override func handle(_ event: NSEvent!, client sender: Any!) -> Bool {
        MainActor.assumeIsolated {
            guard let client = sender as? IMKTextInput, let event else { return false }
            editor = client
            if IsSecureEventInputEnabled() {
                pinyin?.clear()
                if hasMarkedText { updatePinyin(client, start: nil) }
                resetContext()
                return false
            }
            guard event.type == .keyDown, event.modifierFlags.intersection([.command, .control, .option]).isEmpty else {
                commitComposition(sender)
                return false
            }
            if !hasMarkedText, caret != nil, !matchesCaret(client) { resetContext() }
            let code = event.keyCode
            if !hasMarkedText && !flow.choices.isEmpty && !event.modifierFlags.contains(.shift) {
                if code == 48 { return accept(0, client: client) }
                if let s = event.characters, let index = Int(s), (1...flow.choices.count).contains(index) {
                    return accept(index - 1, client: client)
                }
            }
            if !hasMarkedText && code == 53 {
                let active = flow.input != nil && (!flow.ended || !flow.choices.isEmpty)
                cancelPrediction()
                return active
            }
            if !hasMarkedText && code == 48 && flow.isWaiting && !event.modifierFlags.contains(.shift) {
                // A request has started but its first row may not exist yet. Reserve Tab only
                // after the user has already accepted part of this response.
                if flow.input?.accepted.isEmpty == false { return true }
            }
            cancelPrediction()
            let range = client.selectedRange()
            let start: Int? = range.location == NSNotFound ? nil : range.location
            if range.length != 0 && !hasMarkedText { resetContext() }

            if (code == 36 || code == 76), hasMarkedText, let pinyin,
               !pinyin.rawInput.isEmpty, pinyin.rawInput.allSatisfy({ $0.isASCII && ($0.isLetter || $0 == "'") }) {
                let text = EnglishSpacing.join(pinyin.rawInput, after: context)
                pinyin.clear()
                insert(text, at: compositionStart ?? start, in: client)
                compositionStart = nil
                panel.hide()
                if Preferences.enabled, caret != nil { schedule(PredictionInput(input: context), delayed: true) }
                return true
            }
            let navigation: [UInt16:Int32] = [36:0xff0d, 76:0xff0d, 51:0xff08, 53:0xff1b,
                                             123:0xff51, 124:0xff53, 125:0xff54, 126:0xff52,
                                             116:0xff55, 121:0xff56, 117:0xffff]
            let symbol: Int32?
            if let nav = navigation[code] { symbol = hasMarkedText ? nav : nil }
            else if let text = event.characters, text.utf8.count == 1, let ascii = text.utf8.first,
                    (32...126).contains(ascii) { symbol = Int32(ascii) }
            else { symbol = nil }
            if let symbol, pinyin?.press(symbol) == true {
                updatePinyin(client, start: start)
                return true
            }
            // If Rime finalized a composition while forwarding a key, deliver that commit first.
            if hasMarkedText { updatePinyin(client, start: start) }
            resetContext()
            return false
        }
    }

    override func commitComposition(_ sender: Any!) {
        MainActor.assumeIsolated {
            if hasMarkedText, let client = sender as? IMKTextInput ?? editor {
                pinyin?.finish()
                updatePinyin(client, start: compositionStart)
            }
            resetContext()
        }
    }

    override func deactivateServer(_ sender: Any!) {
        MainActor.assumeIsolated {
            commitComposition(sender)
            editor = nil
            super.deactivateServer(sender)
        }
    }

    override func inputControllerWillClose() {
        MainActor.assumeIsolated {
            resetContext()
            NotificationCenter.default.removeObserver(self)
            super.inputControllerWillClose()
        }
    }

    override func menu() -> NSMenu! {
        let menu = NSMenu()
        let item = menu.addItem(withTitle: "DeepSeek 设置…", action: #selector(settings(_:)), keyEquivalent: "")
        item.target = self
        return menu
    }

    @objc func settings(_ sender: Any?) { SettingsWindow.shared.show() }
}
