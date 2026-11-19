// What is left when the reader closes by itself, and what the next start does
// with it, as Android's CrashReports: the core's note of a fatal signal
// (dmc_crash_install), and the name of the file that was being opened. The
// next start shows the note, and that file opens as its description instead
// of closing the reader again.
import CDMCReader
import Foundation

enum CrashReports {
    private static let openingKey = "crash.opening"
    private static let closedOnKey = "crash.closedOn"

    private static var noteURL: URL? {
        guard let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
        else { return nil }
        let folder = support.appendingPathComponent("DMC Native Reader", isDirectory: true)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder.appendingPathComponent("crash_native.txt")
    }

    /// Once per process, at start: returns the report of the last run's crash, if it ended in one.
    static func install() -> String? {
        let defaults = UserDefaults.standard
        var report: [String] = []
        if let opening = defaults.string(forKey: openingKey) {
            report.append("The reader closed while opening \(opening).")
            defaults.set(opening, forKey: closedOnKey)  // it opens as its description next time
            defaults.removeObject(forKey: openingKey)
        }
        if let url = noteURL, let note = try? String(contentsOf: url, encoding: .utf8), !note.isEmpty {
            report.append(note)
            try? FileManager.default.removeItem(at: url)
        }
        if let url = noteURL { url.path.withCString { dmc_crash_install($0) } }
        return report.isEmpty ? nil : report.joined(separator: "\n\n")
    }

    /// A file starts to open: remembered at once, so a crash right after is its.
    static func opening(_ name: String) {
        UserDefaults.standard.set(name, forKey: openingKey)
        UserDefaults.standard.synchronize()
        "opening \(name)".withCString { dmc_crash_context($0) }
    }

    /// `name` is shown (or its open ended).
    static func shown(_ name: String) {
        if UserDefaults.standard.string(forKey: openingKey) == name {
            UserDefaults.standard.removeObject(forKey: openingKey)
        }
        "viewing \(name)".withCString { dmc_crash_context($0) }
    }

    /// True once for the file the reader closed on: it opens as its description.
    static func takeClosedOn(_ name: String) -> Bool {
        guard UserDefaults.standard.string(forKey: closedOnKey) == name else { return false }
        UserDefaults.standard.removeObject(forKey: closedOnKey)
        return true
    }

    /// The app is quitting normally: an open in progress is not a crash.
    static func left() {
        UserDefaults.standard.removeObject(forKey: openingKey)
    }
}
