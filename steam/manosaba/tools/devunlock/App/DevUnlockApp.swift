import SwiftUI

// Tiny development-signed placeholder: installing it makes iPadOS show the Developer Mode
// switch (Settings > Privacy & Security), which then allows wireless pairing with Xcode.
@main
struct DevUnlockApp: App {
    var body: some Scene {
        WindowGroup {
            VStack(spacing: 12) {
                Text("开发者模式解锁用").font(.title)
                Text("设置 → 隐私与安全性 → 开发者模式 → 打开并重启").foregroundStyle(.secondary)
            }.padding()
        }
    }
}
