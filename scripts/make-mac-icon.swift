// Make the Mac app icon (platform/icon/derp.icns) from the DERP wordmark.
// Usage: swift scripts/make-mac-icon.swift
// The wordmark sits on a dark rounded tile; from 256 pixels up it is drawn
// at whole multiples of its pixels, so the pixel art stays crisp.
import AppKit

let project = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let folder = project.appendingPathComponent("platform/icon")
guard let logo = NSImage(contentsOf: folder.appendingPathComponent("derp-logo.png")),
      let logoImage = logo.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
    fatalError("platform/icon/derp-logo.png is missing")
}

func render(_ size: Int) -> Data {
    let s = CGFloat(size)
    let space = CGColorSpace(name: CGColorSpace.sRGB)!
    let context = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
                            space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    // Apple's icon grid: an 824/1024 tile with rounded corners.
    let inset = s * 100 / 1024
    let tile = CGRect(x: inset, y: inset, width: s - 2 * inset, height: s - 2 * inset)
    let path = CGPath(roundedRect: tile, cornerWidth: s * 185 / 1024, cornerHeight: s * 185 / 1024, transform: nil)
    context.saveGState()
    context.setShadow(offset: CGSize(width: 0, height: -s * 10 / 1024), blur: s * 20 / 1024,
                      color: CGColor(gray: 0, alpha: 0.5))
    context.addPath(path)
    context.setFillColor(CGColor(gray: 0.05, alpha: 1))
    context.fillPath()
    context.restoreGState()
    context.saveGState()
    context.addPath(path)
    context.clip()
    let glow = CGGradient(colorsSpace: space, colors: [CGColor(red: 0.16, green: 0.03, blue: 0.02, alpha: 1),
                                                       CGColor(red: 0.03, green: 0.02, blue: 0.02, alpha: 1)] as CFArray,
                          locations: [0, 1])!
    context.drawRadialGradient(glow, startCenter: CGPoint(x: s / 2, y: s / 2), startRadius: 0,
                               endCenter: CGPoint(x: s / 2, y: s / 2), endRadius: tile.width * 0.7, options: [])
    // 4x the wordmark's pixels at 1024 (624 of the 824 tile).
    let scale = s / 256
    let w = CGFloat(logoImage.width) * scale, h = CGFloat(logoImage.height) * scale
    context.interpolationQuality = scale >= 1 ? .none : .high
    context.draw(logoImage, in: CGRect(x: ((s - w) / 2).rounded(), y: ((s - h) / 2).rounded(), width: w, height: h))
    context.restoreGState()
    let bitmap = NSBitmapImageRep(cgImage: context.makeImage()!)
    return bitmap.representation(using: .png, properties: [:])!
}

let iconset = FileManager.default.temporaryDirectory.appendingPathComponent("derp.iconset")
try? FileManager.default.removeItem(at: iconset)
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
for base in [16, 32, 128, 256, 512] {
    try render(base).write(to: iconset.appendingPathComponent("icon_\(base)x\(base).png"))
    try render(base * 2).write(to: iconset.appendingPathComponent("icon_\(base)x\(base)@2x.png"))
}
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", iconset.path, "-o", folder.appendingPathComponent("derp.icns").path]
try iconutil.run()
iconutil.waitUntilExit()
try? FileManager.default.removeItem(at: iconset)
print(iconutil.terminationStatus == 0 ? "Wrote platform/icon/derp.icns" : "iconutil failed")
