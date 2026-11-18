// swift-tools-version:5.9
// Geometry and file format of "Raumklang Messen", without ARKit, so it is tested on any platform.
import PackageDescription

let package = Package(
    name: "SurveyKit",
    platforms: [.iOS(.v16), .macOS(.v13)],
    products: [.library(name: "SurveyKit", targets: ["SurveyKit"])],
    targets: [
        .target(name: "SurveyKit"),
        .testTarget(name: "SurveyKitTests", dependencies: ["SurveyKit"]),
    ]
)
