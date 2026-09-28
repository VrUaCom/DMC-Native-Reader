import SwiftUI

@main
struct DMCReaderApp: App {
    @StateObject private var store = ResourceStore()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(store)
                // Files app "Open with" and Share-sheet opens arrive here.
                .onOpenURL { url in
                    store.open(url: url)
                }
                .preferredColorScheme(.dark)
        }
    }
}
