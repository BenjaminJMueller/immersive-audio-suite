import XCTest
@testable import SurveyKit

final class SurveyKitTests: XCTestCase {
    // ARKit: y up, the camera looks along -z at start. Listener stands at (1, 0, 2) on the floor
    // and faces -z (the front point is 3 m further along -z).
    let floor = Vec3(1, 0, 2)
    let front = Vec3(1, 1.5, -1)

    func testFrameAxes() throws {
        let f = try XCTUnwrap(SurveyFrame(listenerFloor: floor, earHeight: 1.2, frontPoint: front))
        XCTAssertEqual(f.front.z, -1, accuracy: 1e-12)
        XCTAssertEqual(f.right.x, 1, accuracy: 1e-12) // facing -z, right is +x
        // 2 m in front of the ears.
        let p = f.position(Vec3(1, 1.2, 0))
        XCTAssertEqual(p.x, 0, accuracy: 1e-12)
        XCTAssertEqual(p.y, 2, accuracy: 1e-12)
        XCTAssertEqual(p.z, 0, accuracy: 1e-12)
        // 1 m to the right and 0.8 m above the ears.
        let r = f.position(Vec3(2, 2.0, 2))
        XCTAssertEqual(r.x, 1, accuracy: 1e-12)
        XCTAssertEqual(r.y, 0, accuracy: 1e-12)
        XCTAssertEqual(r.z, 0.8, accuracy: 1e-12)
        // Left is positive azimuth in Raumklang.
        XCTAssertEqual(polar(f.position(Vec3(0, 1.2, 2))).azimuth, 90, accuracy: 1e-9)
    }

    func testFrameIgnoresHeightOfFrontPoint() throws {
        let a = try XCTUnwrap(SurveyFrame(listenerFloor: floor, earHeight: 1.2, frontPoint: Vec3(1, 5, -1)))
        let b = try XCTUnwrap(SurveyFrame(listenerFloor: floor, earHeight: 1.2, frontPoint: Vec3(1, -3, -1)))
        XCTAssertEqual(a, b)
    }

    func testRotatedListener() throws {
        // Facing +x: front = +x, right = +z.
        let f = try XCTUnwrap(SurveyFrame(listenerFloor: .zero, earHeight: 1.0, frontPoint: Vec3(4, 1, 0)))
        let p = f.position(Vec3(0, 1, 3))
        XCTAssertEqual(p.x, 3, accuracy: 1e-12)
        XCTAssertEqual(p.y, 0, accuracy: 1e-12)
        let d = f.direction(Vec3(-2, 0, 0)) // pointing back to the listener from the front
        XCTAssertEqual(d.y, -1, accuracy: 1e-12)
    }

    func testInvalidFrames() {
        XCTAssertNil(SurveyFrame(listenerFloor: floor, earHeight: 1.2, frontPoint: Vec3(1, 3, 2)))     // straight up
        XCTAssertNil(SurveyFrame(listenerFloor: floor, earHeight: 1.2, frontPoint: Vec3(1.05, 1, 2.05))) // too close
        XCTAssertNil(SurveyFrame(listenerFloor: Vec3(.nan, 0, 0), earHeight: 1.2, frontPoint: front))
        XCTAssertNil(SurveyFrame(listenerFloor: floor, earHeight: .infinity, frontPoint: front))
        let f = SurveyFrame(listenerFloor: floor, earHeight: 99, frontPoint: front)
        XCTAssertEqual(f?.earHeight, 3)
    }

    func testBuilderAndFile() throws {
        let f = try XCTUnwrap(SurveyFrame(listenerFloor: floor, earHeight: 1.2, frontPoint: front))
        var b = SurveyBuilder(frame: f)
        // A speaker 3 m in front, 1 m above the ears, facing back at the listener.
        b.taps.append(.init(position: Vec3(1, 2.2, -1), normal: Vec3(0, 0, 1), fromLiDAR: true))
        // A broken tap is skipped.
        b.taps.append(.init(position: Vec3(.nan, 0, 0), normal: nil, fromLiDAR: false))
        b.taps.append(.init(position: Vec3(-2, 1.2, 2), normal: nil, fromLiDAR: false))
        let speakers = b.speakers()
        XCTAssertEqual(speakers.count, 2)
        XCTAssertEqual(speakers[0].position[1], 3, accuracy: 1e-12)
        XCTAssertEqual(speakers[0].position[2], 1, accuracy: 1e-12)
        XCTAssertEqual(speakers[0].aim[1], -1, accuracy: 1e-12)
        XCTAssertEqual(speakers[1].aim, [0, 0, 0])
        XCTAssertEqual(speakers[1].confidence, 0.5)

        let file = b.file(source: "iPhone (LiDAR)", created: Date(timeIntervalSince1970: 0))
        let data = try file.encoded()
        let json = try XCTUnwrap(String(data: data, encoding: .utf8))
        XCTAssertTrue(json.contains("\"format\" : \"raumklang-survey\""))
        XCTAssertTrue(json.contains("\"frame\" : \"raumklang\""))
        XCTAssertEqual(try SurveyFile.decode(data), file)
        XCTAssertTrue(SurveyFile.fileName(for: Date(timeIntervalSince1970: 0)).hasSuffix(".raumklang-messung"))
    }

    func testConfidenceIsClamped() {
        XCTAssertEqual(SurveySpeaker(name: "1", position: .zero, aim: .zero, confidence: 7).confidence, 1)
        XCTAssertEqual(SurveySpeaker(name: "1", position: .zero, aim: .zero, confidence: .nan).confidence, 0)
    }
}
