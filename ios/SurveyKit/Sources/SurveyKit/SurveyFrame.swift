import Foundation

/// A 3D vector in metres (AR world or Raumklang coordinates).
public struct Vec3: Equatable, Codable, Sendable {
    public var x: Double
    public var y: Double
    public var z: Double

    public init(_ x: Double, _ y: Double, _ z: Double) {
        self.x = x
        self.y = y
        self.z = z
    }

    public static let zero = Vec3(0, 0, 0)
    public static func + (a: Vec3, b: Vec3) -> Vec3 { Vec3(a.x + b.x, a.y + b.y, a.z + b.z) }
    public static func - (a: Vec3, b: Vec3) -> Vec3 { Vec3(a.x - b.x, a.y - b.y, a.z - b.z) }
    public static func * (a: Vec3, s: Double) -> Vec3 { Vec3(a.x * s, a.y * s, a.z * s) }
    public func dot(_ o: Vec3) -> Double { x * o.x + y * o.y + z * o.z }
    public func cross(_ o: Vec3) -> Vec3 { Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x) }
    public var length: Double { dot(self).squareRoot() }
    public var isFinite: Bool { x.isFinite && y.isFinite && z.isFinite }
    public var normalized: Vec3? {
        let l = length
        guard l > 1e-9, l.isFinite else { return nil }
        return self * (1.0 / l)
    }
}

/// Converts ARKit world coordinates (metres, y up, gravity aligned, right handed) into the
/// Raumklang frame: origin at the listener's ears, x right, y front, z up.
public struct SurveyFrame: Equatable, Sendable {
    /// Listener's ears in AR world coordinates.
    public let origin: Vec3
    /// Horizontal unit vectors in AR world coordinates.
    public let front: Vec3
    public let right: Vec3
    public let earHeight: Double

    static let up = Vec3(0, 1, 0)

    /// `listenerFloor`: the floor point tapped at the listening position. `frontPoint`: any point in
    /// the direction the audience faces (e.g. the centre speaker). Fails if the front point is
    /// (almost) straight above or below the listener, or a value is not finite.
    public init?(listenerFloor: Vec3, earHeight: Double, frontPoint: Vec3) {
        guard listenerFloor.isFinite, frontPoint.isFinite, earHeight.isFinite else { return nil }
        let h = min(max(earHeight, 0.0), 3.0)
        let origin = listenerFloor + Vec3(0, h, 0)
        var f = frontPoint - origin
        f.y = 0
        guard f.length > 0.2, let front = f.normalized else { return nil }
        // Right handed, y up: front x up = right.
        guard let right = front.cross(SurveyFrame.up).normalized else { return nil }
        self.origin = origin
        self.front = front
        self.right = right
        self.earHeight = h
    }

    /// A position in the Raumklang frame.
    public func position(_ world: Vec3) -> Vec3 {
        let d = world - origin
        return Vec3(d.dot(right), d.dot(front), d.y)
    }

    /// A direction in the Raumklang frame (unit length, or zero if unknown).
    public func direction(_ world: Vec3) -> Vec3 {
        guard let n = world.normalized else { return .zero }
        return Vec3(n.dot(right), n.dot(front), n.y)
    }
}
