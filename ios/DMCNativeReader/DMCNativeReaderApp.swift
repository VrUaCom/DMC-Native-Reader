import SwiftUI

@main
struct DMCNativeReaderApp: App {
    @StateObject private var store = ResourceStore()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(store)
                .onOpenURL { store.open(url: $0) }
                .preferredColorScheme(.dark)
        }
    }
}
