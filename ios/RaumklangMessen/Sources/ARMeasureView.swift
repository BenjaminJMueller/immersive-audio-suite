import ARKit
import RealityKit
import SurveyKit
import SwiftUI
import UIKit

/// Camera view with AR tracking. With LiDAR the taps hit the real surfaces (the speaker front),
/// and the surface normal gives the direction the speaker faces.
struct ARMeasureView: UIViewRepresentable {
    @ObservedObject var model: MeasureModel

    func makeCoordinator() -> Coordinator { Coordinator(model: model) }

    func makeUIView(context: Context) -> ARView {
        let view = ARView(frame: .zero, cameraMode: .ar, automaticallyConfigureSession: false)
        let config = ARWorldTrackingConfiguration()
        config.worldAlignment = .gravity
        config.planeDetection = [.horizontal, .vertical]
        let lidar = ARWorldTrackingConfiguration.supportsSceneReconstruction(.mesh)
        if lidar { config.sceneReconstruction = .mesh }
        if ARWorldTrackingConfiguration.supportsFrameSemantics(.sceneDepth) { config.frameSemantics.insert(.sceneDepth) }
        view.session.delegate = context.coordinator
        view.session.run(config)
        view.renderOptions.insert(.disableMotionBlur)

        let coaching = ARCoachingOverlayView()
        coaching.session = view.session
        coaching.goal = .anyPlane
        coaching.activatesAutomatically = true
        coaching.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(coaching)
        NSLayoutConstraint.activate([
            coaching.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            coaching.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            coaching.topAnchor.constraint(equalTo: view.topAnchor),
            coaching.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])

        let tap = UITapGestureRecognizer(target: context.coordinator, action: #selector(Coordinator.tapped(_:)))
        view.addGestureRecognizer(tap)
        context.coordinator.view = view
        let coordinator = context.coordinator
        DispatchQueue.main.async {
            model.hasLiDAR = lidar
            model.placeAtCrosshair = { [weak coordinator] in coordinator?.placeAtCentre() }
            model.markersChanged = { [weak coordinator] in coordinator?.rebuildMarkers() }
        }
        return view
    }

    func updateUIView(_ uiView: ARView, context: Context) {}

    static func dismantleUIView(_ uiView: ARView, coordinator: Coordinator) {
        uiView.session.pause()
    }

    final class Coordinator: NSObject, ARSessionDelegate {
        let model: MeasureModel
        weak var view: ARView?
        private var anchors: [AnchorEntity] = []

        init(model: MeasureModel) { self.model = model }

        @objc func tapped(_ g: UITapGestureRecognizer) {
            guard let view else { return }
            place(at: g.location(in: view))
        }

        func placeAtCentre() {
            guard let view else { return }
            place(at: CGPoint(x: view.bounds.midX, y: view.bounds.midY))
        }

        private func place(at point: CGPoint) {
            guard let view else { return }
            // Real surfaces first (LiDAR depth / estimated planes), then known planes.
            let hit = view.raycast(from: point, allowing: .estimatedPlane, alignment: .any).first
                ?? view.raycast(from: point, allowing: .existingPlaneGeometry, alignment: .any).first
            guard let hit else {
                Task { @MainActor in self.model.missed() }
                return
            }
            let t = hit.worldTransform
            let p = Vec3(Double(t.columns.3.x), Double(t.columns.3.y), Double(t.columns.3.z))
            // The y axis of the hit transform is the surface normal.
            var n = Vec3(Double(t.columns.1.x), Double(t.columns.1.y), Double(t.columns.1.z))
            // The normal must point towards the camera side (the front of the speaker).
            if let cam = view.session.currentFrame?.camera.transform {
                let c = Vec3(Double(cam.columns.3.x), Double(cam.columns.3.y), Double(cam.columns.3.z))
                if n.dot(c - p) < 0 { n = n * -1 }
            }
            let lidar = ARWorldTrackingConfiguration.supportsSceneReconstruction(.mesh)
            let feedback = UIImpactFeedbackGenerator(style: .light)
            feedback.impactOccurred()
            Task { @MainActor in self.model.place(position: p, normal: n.normalized, fromLiDAR: lidar) }
        }

        // MARK: Markers

        @MainActor func rebuildMarkers() {
            guard let view else { return }
            for a in anchors { view.scene.removeAnchor(a) }
            anchors.removeAll()
            if let l = model.listenerFloor { add(marker(colour: UIColor(Theme.ok), label: "Hörplatz"), at: l, normal: nil) }
            if let f = model.frontPoint { add(marker(colour: UIColor(Theme.warn), label: "Vorne"), at: f, normal: nil) }
            for (i, t) in model.taps.enumerated() {
                add(marker(colour: UIColor(Theme.accent), label: String(i + 1)), at: t.position, normal: t.normal)
            }
        }

        private func marker(colour: UIColor, label: String) -> Entity {
            let root = Entity()
            let dot = ModelEntity(mesh: .generateSphere(radius: 0.025), materials: [UnlitMaterial(color: colour)])
            root.addChild(dot)
            let text = ModelEntity(mesh: .generateText(label, extrusionDepth: 0.002, font: .systemFont(ofSize: 0.06, weight: .semibold),
                                                       containerFrame: .zero, alignment: .center, lineBreakMode: .byWordWrapping),
                                   materials: [UnlitMaterial(color: .white)])
            text.position = [0, 0.05, 0]
            text.name = "label"
            root.addChild(text)
            return root
        }

        @MainActor private func add(_ entity: Entity, at p: Vec3, normal: Vec3?) {
            guard let view else { return }
            let anchor = AnchorEntity(world: SIMD3<Float>(Float(p.x), Float(p.y), Float(p.z)))
            anchor.addChild(entity)
            if let n = normal?.normalized {
                // Thin arrow along the normal: where the speaker points.
                let dir = SIMD3<Float>(Float(n.x), Float(n.y), Float(n.z))
                let arrow = ModelEntity(mesh: .generateBox(size: [0.008, 0.008, 0.18]), materials: [UnlitMaterial(color: UIColor(Theme.accent))])
                arrow.orientation = simd_quatf(from: [0, 0, 1], to: simd_normalize(dir))
                arrow.position = dir * 0.09
                anchor.addChild(arrow)
            }
            // Labels face the camera at the moment they are placed.
            if let cam = view.session.currentFrame?.camera.transform, let label = entity.findEntity(named: "label") {
                let c = SIMD3<Float>(cam.columns.3.x, cam.columns.3.y, cam.columns.3.z)
                let here = SIMD3<Float>(Float(p.x), Float(p.y) + 0.05, Float(p.z))
                label.look(at: here + (here - c), from: label.position(relativeTo: nil), relativeTo: nil)
            }
            view.scene.addAnchor(anchor)
            anchors.append(anchor)
        }

        // MARK: Tracking state

        func session(_ session: ARSession, cameraDidChangeTrackingState camera: ARCamera) {
            let ok: Bool
            if case .normal = camera.trackingState { ok = true } else { ok = false }
            Task { @MainActor in self.model.trackingOK = ok }
        }
    }
}
