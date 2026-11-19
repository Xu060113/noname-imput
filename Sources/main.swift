import Cocoa
import Carbon
import InputMethodKit

let sourceID = "local.yuju.inputmethod.Yuju.Hans"

func registerInputSource() -> Int32 {
    let status = TISRegisterInputSource(Bundle.main.bundleURL as CFURL)
    guard status == noErr else { fputs("输入法注册失败 \(status)\n", stderr); return 1 }
    let parentQuery = [kTISPropertyInputSourceID as String: Bundle.main.bundleIdentifier!] as CFDictionary
    for parent in TISCreateInputSourceList(parentQuery, true).takeRetainedValue() as! [TISInputSource] {
        let result = TISEnableInputSource(parent)
        if result != noErr { print("输入法组件状态：\(result)") }
    }
    let query = [kTISPropertyInputSourceID as String: sourceID] as CFDictionary
    let sources = TISCreateInputSourceList(query, true).takeRetainedValue() as! [TISInputSource]
    guard let source = sources.first else {
        print("已注册。在系统设置 → 键盘 → 输入法中添加「语句拼音」。")
        return 0
    }
    let enabled = TISEnableInputSource(source)
    if enabled == noErr { print("「语句拼音」已启用，可从菜单栏输入法列表切换。") }
    else { print("已注册。请在系统设置 → 键盘 → 输入法中启用「语句拼音」（\(enabled)）。") }
    return 0
}

@MainActor
final class ApplicationDelegate: NSObject, NSApplicationDelegate {
    func application(_ application: NSApplication, open urls: [URL]) {
        if urls.contains(where: { $0.scheme == "yujupinyin" && $0.host == "settings" }) { SettingsWindow.shared.show() }
    }
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        SettingsWindow.shared.show()
        return false
    }
}

if CommandLine.arguments.contains("--register") { exit(registerInputSource()) }
MainActor.assumeIsolated {
    let app = NSApplication.shared
    let appDelegate = ApplicationDelegate()
    app.delegate = appDelegate
    app.setActivationPolicy(.accessory)
    let sharedData = Bundle.main.resourceURL!.appendingPathComponent("Rime")
    let userData = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("Yuju/Rime", isDirectory: true)
    guard PinyinSession.start(withData: sharedData.path, user: userData.path) else {
        fputs("无法初始化拼音词库。\n", stderr)
        exit(1)
    }
    let connectionName = Bundle.main.object(forInfoDictionaryKey: "InputMethodConnectionName") as! String
    let server = IMKServer(name: connectionName, bundleIdentifier: Bundle.main.bundleIdentifier!)
    withExtendedLifetime((server, appDelegate)) { app.run() }
}
