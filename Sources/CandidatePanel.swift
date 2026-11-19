import AppKit
import InputMethodKit

@MainActor
final class CandidatePanel {
    private let window = NSPanel(contentRect: .zero, styleMask: [.borderless, .nonactivatingPanel],
                                 backing: .buffered, defer: false)
    private let label = NSTextField(labelWithString: "")
    init() {
        window.isOpaque = false
        window.backgroundColor = .clear
        window.hasShadow = true
        window.hidesOnDeactivate = false
        window.ignoresMouseEvents = true
        window.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .transient]
        let surface = NSVisualEffectView()
        surface.material = .popover
        surface.state = .active
        surface.wantsLayer = true
        surface.layer?.cornerRadius = 8
        surface.addSubview(label)
        window.contentView = surface
        label.font = .systemFont(ofSize: 16)
    }

    func hide() { window.orderOut(nil) }

    func show(_ words: [String], highlight: Int = 0, ai: Bool, client: IMKTextInput) {
        guard !words.isEmpty else { hide(); return }
        let text = NSMutableAttributedString()
        for (index, word) in words.enumerated() {
            if index > 0 { text.append(NSAttributedString(string: "    ")) }
            text.append(NSAttributedString(string: "\(index + 1) ", attributes: [
                .font: NSFont.monospacedDigitSystemFont(ofSize: 12, weight: .medium),
                .foregroundColor: NSColor.secondaryLabelColor]))
            text.append(NSAttributedString(string: word, attributes: [
                .font: NSFont.systemFont(ofSize: 16, weight: index == highlight ? .semibold : .regular),
                .foregroundColor: index == highlight && ai ? NSColor.controlAccentColor : NSColor.labelColor]))
        }
        if ai {
            text.append(NSAttributedString(string: "   ⇥", attributes: [
                .font: NSFont.systemFont(ofSize: 12), .foregroundColor: NSColor.secondaryLabelColor]))
        }
        label.attributedStringValue = text
        label.sizeToFit()
        var rect = NSRect.zero
        _ = client.attributes(forCharacterIndex: 0, lineHeightRectangle: &rect)
        let point = rect == .zero ? NSEvent.mouseLocation : rect.origin
        guard let screen = NSScreen.screens.first(where: { $0.frame.contains(point) }) ?? NSScreen.main else { return }
        let size = NSSize(width: min(ceil(label.frame.width) + 24, screen.visibleFrame.width), height: 34)
        let bounds = screen.visibleFrame
        let x = min(max(point.x, bounds.minX), bounds.maxX - size.width)
        let below = point.y - size.height - 3
        let y = below >= bounds.minY ? below : min(bounds.maxY - size.height, point.y + rect.height + 3)
        window.level = NSWindow.Level(rawValue: max(NSWindow.Level.popUpMenu.rawValue, Int(client.windowLevel()) + 1))
        window.setFrame(NSRect(origin: NSPoint(x: x, y: y), size: size), display: true)
        label.setFrameOrigin(NSPoint(x: 12, y: (size.height - label.frame.height) / 2))
        window.orderFrontRegardless()
    }
}
