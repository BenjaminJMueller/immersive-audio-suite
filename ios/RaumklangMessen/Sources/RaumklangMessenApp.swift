import SwiftUI

@main
struct RaumklangMessenApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .preferredColorScheme(.dark)
                .tint(Theme.accent)
        }
    }
}

/// The same colours as the Raumklang desktop app (Projection Studio style).
enum Theme {
    static let accent = Color(red: 0x0a / 255.0, green: 0x84 / 255.0, blue: 0xff / 255.0)
    static let ok = Color(red: 0x32 / 255.0, green: 0xc7 / 255.0, blue: 0x66 / 255.0)
    static let warn = Color(red: 0xf0 / 255.0, green: 0xa5 / 255.0, blue: 0x31 / 255.0)
    static let bar = Color(red: 0x2a / 255.0, green: 0x2a / 255.0, blue: 0x2c / 255.0)
}
