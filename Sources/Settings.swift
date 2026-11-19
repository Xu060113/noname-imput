import AppKit
import Security

enum Preferences {
    static let changed = Notification.Name("YujuPreferencesChanged")
    static var enabled: Bool { UserDefaults.standard.bool(forKey: "predictionEnabled") }
    static var lastIssue: String?
    private static let item: [String:Any] = [
        kSecClass as String: kSecClassGenericPassword,
        kSecAttrService as String: "local.yuju.inputmethod.DeepSeek",
        kSecAttrAccount as String: "API Key"
    ]

    static func key() throws -> String {
        var query = item
        query[kSecReturnData as String] = true
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        if status == errSecItemNotFound { return "" }
        guard status == errSecSuccess else { throw NSError(domain: NSOSStatusErrorDomain, code: Int(status)) }
        return (result as? Data).flatMap { String(data: $0, encoding: .utf8) } ?? ""
    }

    static func save(key: String, enabled: Bool) throws {
        let status: OSStatus
        if key.isEmpty {
            status = SecItemDelete(item as CFDictionary)
        } else {
            let attrs: [String:Any] = [kSecValueData as String: Data(key.utf8)]
            let update = SecItemUpdate(item as CFDictionary, attrs as CFDictionary)
            if update == errSecItemNotFound {
                var addition = item.merging(attrs) { _, new in new }
                addition[kSecAttrAccessible as String] = kSecAttrAccessibleWhenUnlockedThisDeviceOnly
                status = SecItemAdd(addition as CFDictionary, nil)
            } else { status = update }
        }
        guard status == errSecSuccess || (key.isEmpty && status == errSecItemNotFound) else {
            throw NSError(domain: NSOSStatusErrorDomain, code: Int(status))
        }
        UserDefaults.standard.set(enabled && !key.isEmpty, forKey: "predictionEnabled")
        lastIssue = nil
        NotificationCenter.default.post(name: changed, object: nil)
    }
}

@MainActor
final class SettingsWindow: NSObject, NSWindowDelegate {
    static let shared = SettingsWindow()
    private let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 466, height: 298),
                                  styleMask: [.titled, .closable], backing: .buffered, defer: false)
    private let field = NSSecureTextField()
    private let toggle = NSButton(checkboxWithTitle: "停顿后显示 AI 候选", target: nil, action: nil)
    private let status = NSTextField(wrappingLabelWithString: "")
    private var probe: Task<Void, Never>?
    private var keyLoaded = false

    override init() {
        super.init()
        window.title = "语句拼音 · DeepSeek 设置"
        window.isReleasedWhenClosed = false
        window.delegate = self
        field.placeholderString = "sk-…"
        let help = NSTextField(wrappingLabelWithString: "开启后，本次连续输入的片段会发送给 DeepSeek 以预测下文。API Key 保存在这台 Mac 的钥匙串中。")
        help.textColor = .secondaryLabelColor
        let test = NSButton(title: "测试连接", target: self, action: #selector(testConnection))
        test.toolTip = "用「什么是大模型」测试回答和首组选词延迟"
        let save = NSButton(title: "保存", target: self, action: #selector(save))
        save.keyEquivalent = "\r"
        let buttons = NSStackView(views: [test, save])
        let stack = NSStackView(views: [NSTextField(labelWithString: "DeepSeek API Key"), field, toggle, help, buttons, status])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 12
        stack.translatesAutoresizingMaskIntoConstraints = false
        window.contentView!.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: window.contentView!.leadingAnchor, constant: 24),
            stack.trailingAnchor.constraint(equalTo: window.contentView!.trailingAnchor, constant: -24),
            stack.topAnchor.constraint(equalTo: window.contentView!.topAnchor, constant: 22),
            field.widthAnchor.constraint(equalTo: stack.widthAnchor),
            help.widthAnchor.constraint(equalTo: stack.widthAnchor),
            status.widthAnchor.constraint(equalTo: stack.widthAnchor)
        ])
        window.center()
    }

    func show() {
        do {
            field.stringValue = try Preferences.key()
            keyLoaded = true
            toggle.state = Preferences.enabled || field.stringValue.isEmpty ? .on : .off
            status.stringValue = Preferences.lastIssue ?? ""
        } catch {
            keyLoaded = false
            status.stringValue = "读取钥匙串失败：" + error.localizedDescription
        }
        NSApp.activate(ignoringOtherApps: true)
        window.makeKeyAndOrderFront(nil)
    }

    func windowWillClose(_ notification: Notification) {
        probe?.cancel()
        field.stringValue = ""
    }

    @objc private func save() {
        let key = field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !key.isEmpty || keyLoaded else { return }
        if key.isEmpty && toggle.state == .on {
            status.stringValue = "请填写 API Key，或关闭 AI 候选后保存。"
            return
        }
        do {
            try Preferences.save(key: key, enabled: toggle.state == .on)
            window.close()
        } catch { status.stringValue = "保存失败：" + error.localizedDescription }
    }

    @objc private func testConnection() {
        let key = field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !key.isEmpty else { status.stringValue = "请先填写 API Key。"; return }
        probe?.cancel()
        status.stringValue = "正在测试「什么是大模型」…"
        probe = Task {
            let start = Date()
            var first: Double?
            var answer = ""
            do {
                try await DeepSeek.stream(PredictionInput(input: "什么是大模型"), key: key) { frame in
                    try Task.checkCancellation()
                    if case .words(let words) = frame {
                        if first == nil { first = Date().timeIntervalSince(start) }
                        answer += words[0]
                    }
                }
                try Task.checkCancellation()
                if let first {
                    status.stringValue = String(format: "首组 %.2f 秒　", first) + answer
                } else { status.stringValue = "连接成功，模型没有给出候选。" }
            } catch {
                if !Task.isCancelled { status.stringValue = error.localizedDescription }
            }
        }
    }
}
