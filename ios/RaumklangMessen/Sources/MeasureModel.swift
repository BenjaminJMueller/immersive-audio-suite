import Foundation
import SurveyKit

/// The measuring workflow: 1 listening position, 2 front direction, 3 every speaker in turn.
@MainActor
final class MeasureModel: ObservableObject {
    enum Step: Int, CaseIterable {
        case listener = 0, front = 1, speakers = 2
        var title: String {
            switch self {
            case .listener: return "Hörplatz"
            case .front: return "Vorne"
            case .speakers: return "Lautsprecher"
            }
        }
    }

    @Published private(set) var step: Step = .listener
    @Published var earHeight: Double = 1.2
    @Published private(set) var listenerFloor: Vec3?
    @Published private(set) var frontPoint: Vec3?
    @Published private(set) var taps: [SurveyBuilder.Tap] = []
    @Published var hint: String?
    @Published var hasLiDAR = false
    @Published var trackingOK = false

    /// Set by the AR view: place a point where the crosshair is.
    var placeAtCrosshair: (() -> Void)?
    /// Set by the AR view: markers follow the model (undo, reset).
    var markersChanged: (() -> Void)?

    var frame: SurveyFrame? {
        guard let l = listenerFloor, let f = frontPoint else { return nil }
        return SurveyFrame(listenerFloor: l, earHeight: earHeight, frontPoint: f)
    }

    var coachBadge: String { step == .speakers ? String(taps.count + 1) : String(step.rawValue + 1) }

    var coachTitle: String {
        switch step {
        case .listener: return "Boden am Hörplatz antippen"
        case .front: return "Richtung vorne antippen"
        case .speakers: return "Lautsprecher \(taps.count + 1) antippen"
        }
    }

    var coachText: String {
        if let hint { return hint }
        switch step {
        case .listener: return "Wo das Publikum sitzt, z. B. die Mitte. Die Ohrhöhe unten einstellen."
        case .front: return "Etwas, das vorne liegt, z. B. der mittlere Lautsprecher oder die Wand."
        case .speakers:
            return taps.isEmpty ? "Mitte der Membran anpeilen, in der Reihenfolge der Ausgänge."
                                : "Weiter mit Lautsprecher \(taps.count + 1), oder oben teilen."
        }
    }

    func place(position: Vec3, normal: Vec3?, fromLiDAR: Bool) {
        guard position.isFinite else { return }
        hint = nil
        switch step {
        case .listener:
            listenerFloor = position
            step = .front
        case .front:
            guard let l = listenerFloor, SurveyFrame(listenerFloor: l, earHeight: earHeight, frontPoint: position) != nil else {
                hint = "Zu nah am Hörplatz. Etwas weiter vorne antippen."
                return
            }
            frontPoint = position
            step = .speakers
        case .speakers:
            guard taps.count < 128 else {
                hint = "Mehr als 128 Lautsprecher gehen nicht."
                return
            }
            taps.append(.init(position: position, normal: normal, fromLiDAR: fromLiDAR))
        }
        markersChanged?()
    }

    func missed() { hint = "Keine Fläche gefunden. Etwas näher heran oder langsam bewegen." }

    func undo() {
        hint = nil
        switch step {
        case .speakers:
            if taps.isEmpty { frontPoint = nil; step = .front } else { taps.removeLast() }
        case .front:
            listenerFloor = nil
            step = .listener
        case .listener:
            break
        }
        markersChanged?()
    }

    var canUndo: Bool { step != .listener }
    var canShare: Bool { frame != nil && !taps.isEmpty }

    func reset() {
        taps.removeAll()
        listenerFloor = nil
        frontPoint = nil
        step = .listener
        hint = nil
        markersChanged?()
    }

    /// Speakers in the Raumklang frame, for the list.
    var measured: [SurveySpeaker] {
        guard let f = frame else { return [] }
        var b = SurveyBuilder(frame: f)
        b.taps = taps
        return b.speakers()
    }

    /// Writes the measurement file and returns its URL for the share sheet.
    func exportFile() throws -> URL {
        guard let f = frame else { throw CocoaError(.fileWriteUnknown) }
        var b = SurveyBuilder(frame: f)
        b.taps = taps
        let source = hasLiDAR ? "iPhone (LiDAR)" : "iPhone (AR)"
        let data = try b.file(source: source).encoded()
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(SurveyFile.fileName())
        try data.write(to: url, options: .atomic)
        return url
    }
}
