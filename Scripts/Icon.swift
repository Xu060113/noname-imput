import AppKit

let target = URL(fileURLWithPath: CommandLine.arguments[1]) as CFURL
var box = CGRect(x: 0, y: 0, width: 18, height: 18)
let pdf = CGContext(target, mediaBox: &box, nil)!
pdf.beginPDFPage(nil)
let glyph = NSAttributedString(string: "句", attributes: [.font: NSFont.systemFont(ofSize: 16, weight: .semibold)])
let line = CTLineCreateWithAttributedString(glyph)
let bounds = CTLineGetBoundsWithOptions(line, .useGlyphPathBounds)
pdf.textPosition = CGPoint(x: 9 - bounds.midX, y: 9 - bounds.midY)
CTLineDraw(line, pdf)
pdf.endPDFPage()
pdf.closePDF()
