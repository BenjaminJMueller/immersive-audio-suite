import Foundation

/// One measured speaker in the Raumklang frame.
public struct SurveySpeaker: Codable, Equatable, Sendable {
    public var name: String
    public var position: [Double]
    public var aim: [Double]
    public var confidence: Double

    public init(name: String, position: Vec3, aim: Vec3, confidence: Double) {
        self.name = name
        self.position = [position.x, position.y, position.z]
        self.aim = [aim.x, aim.y, aim.z]
        self.confidence = min(max(confidence.isFinite ? confidence : 0, 0), 1)
    }
}

/// The file Raumklang imports ("iPhone-Messung laden" or drag and drop onto the window).
/// Keys and meaning match rkui::surveyFromJson in the desktop app.
public struct SurveyFile: Codable, Equatable, Sendable {
    public var format = "raumklang-survey"
    public var version = 1
    public var source: String
    public var created: String
    public var frame = "raumklang"
    public var listenerHeight: Double
    public var speakers: [SurveySpeaker]

    public init(source: String, created: Date = Date(), listenerHeight: Double, speakers: [SurveySpeaker]) {
        self.source = source
        let f = ISO8601DateFormatter()
        self.created = f.string(from: created)
        self.listenerHeight = listenerHeight
        self.speakers = speakers
    }

    public func encoded() throws -> Data {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try e.encode(self)
    }

    public static func decode(_ data: Data) throws -> SurveyFile {
        try JSONDecoder().decode(SurveyFile.self, from: data)
    }

    /// File name like "Lautsprecher 2026-11-18 1430.raumklang-messung".
    public static func fileName(for date: Date = Date()) -> String {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyy-MM-dd HHmm"
        return "Lautsprecher \(f.string(from: date)).raumklang-messung"
    }
}

/// Builds the file from AR measurements. Speakers whose aim points at the listener within a few
/// degrees are stored as such; Raumklang then keeps them facing the listener when moved.
public struct SurveyBuilder {
    public struct Tap: Equatable {
        public var position: Vec3 // AR world
        public var normal: Vec3?  // AR world, surface normal at the tap = direction the speaker faces
        public var fromLiDAR: Bool
        public init(position: Vec3, normal: Vec3?, fromLiDAR: Bool) {
            self.position = position
            self.normal = normal
            self.fromLiDAR = fromLiDAR
        }
    }

    public var frame: SurveyFrame
    public var taps: [Tap] = []

    public init(frame: SurveyFrame) { self.frame = frame }

    public func speakers() -> [SurveySpeaker] {
        taps.enumerated().compactMap { i, t in
            guard t.position.isFinite else { return nil }
            let p = frame.position(t.position)
            let aim = t.normal.map { frame.direction($0) } ?? .zero
            return SurveySpeaker(name: String(i + 1), position: p, aim: aim, confidence: t.fromLiDAR ? 0.9 : 0.5)
        }
    }

    public func file(source: String, created: Date = Date()) -> SurveyFile {
        SurveyFile(source: source, created: created, listenerHeight: frame.earHeight, speakers: speakers())
    }
}

/// Azimuth (degrees, 0 = front, positive = left), elevation and distance like Raumklang shows them.
public func polar(_ p: Vec3) -> (azimuth: Double, elevation: Double, distance: Double) {
    let d = p.length
    guard d > 1e-9 else { return (0, 0, 0) }
    let az = atan2(-p.x, p.y) * 180 / Double.pi
    let el = asin(min(max(p.z / d, -1), 1)) * 180 / Double.pi
    return (az, el, d)
}
