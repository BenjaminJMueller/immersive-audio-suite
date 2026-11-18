import SurveyKit
import SwiftUI
import UIKit

struct ContentView: View {
    @StateObject private var model = MeasureModel()
    @State private var shareURL: URL?
    @State private var showHelp = false
    @State private var showList = false
    @State private var exportError: String?
    @AppStorage("introSeen") private var introSeen = false

    var body: some View {
        ZStack {
            ARMeasureView(model: model)
                .ignoresSafeArea()
            Crosshair(active: model.trackingOK)
                .allowsHitTesting(false)
            VStack(spacing: 10) {
                TopBar(model: model, share: share, help: { showHelp = true })
                StepBar(step: model.step)
                Spacer()
                CoachCard(badge: model.coachBadge, title: model.coachTitle, text: model.coachText, warn: model.hint != nil)
                Controls(model: model, showList: { showList = true }, share: share)
            }
            .padding(.horizontal, 16)
            .padding(.bottom, 8)
        }
        .sheet(item: $shareURL) { url in ShareSheet(items: [url]) }
        .sheet(isPresented: $showHelp) { HelpView() }
        .sheet(isPresented: $showList) { SpeakerList(speakers: model.measured) }
        .sheet(isPresented: Binding(get: { !introSeen }, set: { if !$0 { introSeen = true } })) { IntroView { introSeen = true } }
        .alert("Teilen nicht möglich", isPresented: Binding(get: { exportError != nil }, set: { if !$0 { exportError = nil } })) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(exportError ?? "")
        }
    }

    private func share() {
        do { shareURL = try model.exportFile() } catch { exportError = "Die Datei konnte nicht geschrieben werden." }
    }
}

extension URL: Identifiable {
    public var id: String { absoluteString }
}

// MARK: - Top bar: name left, icon buttons right (undo, share, help)

private struct TopBar: View {
    @ObservedObject var model: MeasureModel
    let share: () -> Void
    let help: () -> Void

    var body: some View {
        HStack(spacing: 6) {
            VStack(alignment: .leading, spacing: 1) {
                Text("Raumklang Messen").font(.headline)
                Text(model.hasLiDAR ? "LiDAR aktiv" : "Ohne LiDAR, etwas ungenauer")
                    .font(.caption2)
                    .foregroundStyle(model.hasLiDAR ? Theme.ok : Theme.warn)
            }
            Spacer()
            IconButton(system: "arrow.uturn.backward", label: "Rückgängig", enabled: model.canUndo) { model.undo() }
            IconButton(system: "square.and.arrow.up", label: "An Raumklang senden", enabled: model.canShare, action: share)
            IconButton(system: "questionmark.circle", label: "Hilfe", enabled: true, action: help)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .padding(.top, 4)
    }
}

private struct IconButton: View {
    let system: String
    let label: String
    let enabled: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: system)
                .font(.system(size: 17, weight: .medium))
                .frame(width: 38, height: 38)
                .contentShape(Rectangle())
        }
        .buttonStyle(PressableStyle())
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.35)
        .accessibilityLabel(label)
    }
}

private struct PressableStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(Color.white.opacity(configuration.isPressed ? 0.16 : 0.0001), in: RoundedRectangle(cornerRadius: 9))
            .scaleEffect(configuration.isPressed ? 0.94 : 1)
    }
}

// MARK: - Numbered workflow steps

private struct StepBar: View {
    let step: MeasureModel.Step

    var body: some View {
        HStack(spacing: 4) {
            ForEach(MeasureModel.Step.allCases, id: \.rawValue) { s in
                HStack(spacing: 6) {
                    ZStack {
                        Circle().fill(s.rawValue < step.rawValue ? Theme.ok : (s == step ? Theme.accent : Color.white.opacity(0.18)))
                        if s.rawValue < step.rawValue {
                            Image(systemName: "checkmark").font(.system(size: 10, weight: .bold))
                        } else {
                            Text("\(s.rawValue + 1)").font(.system(size: 11, weight: .bold))
                        }
                    }
                    .frame(width: 20, height: 20)
                    Text(s.title).font(.subheadline.weight(s == step ? .semibold : .regular))
                        .foregroundStyle(s == step ? .primary : .secondary)
                        .lineLimit(1)
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(s == step ? Color.white.opacity(0.12) : .clear, in: Capsule())
            }
        }
        .padding(4)
        .background(.ultraThinMaterial, in: Capsule())
    }
}

// MARK: - Coach card: the next step in plain words

private struct CoachCard: View {
    let badge: String
    let title: String
    let text: String
    let warn: Bool

    var body: some View {
        HStack(alignment: .center, spacing: 12) {
            Text(badge)
                .font(.system(size: 15, weight: .bold))
                .frame(width: 30, height: 30)
                .background(warn ? Theme.warn : Theme.accent, in: Circle())
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.headline)
                Text(text).font(.subheadline).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            }
            Spacer(minLength: 0)
        }
        .padding(14)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
        .animation(.easeOut(duration: 0.2), value: title)
    }
}

// MARK: - Controls under the coach card

private struct Controls: View {
    @ObservedObject var model: MeasureModel
    let showList: () -> Void
    let share: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            switch model.step {
            case .listener:
                Stepper(value: $model.earHeight, in: 0.5...2.2, step: 0.05) {
                    VStack(alignment: .leading, spacing: 0) {
                        Text("Ohrhöhe").font(.caption).foregroundStyle(.secondary)
                        Text(String(format: "%.2f m", model.earHeight)).font(.body.monospacedDigit())
                    }
                }
                .frame(maxWidth: 210)
            case .front:
                EmptyView()
            case .speakers:
                Button(action: showList) {
                    Label("\(model.taps.count)", systemImage: "hifispeaker.2")
                        .font(.body.weight(.semibold))
                        .padding(.horizontal, 14)
                        .frame(height: 50)
                }
                .buttonStyle(.bordered)
                .disabled(model.taps.isEmpty)
                .accessibilityLabel("Gemessene Lautsprecher")
            }
            Spacer(minLength: 0)
            if model.step == .speakers && !model.taps.isEmpty {
                Button("Fertig", action: share)
                    .font(.body.weight(.semibold))
                    .frame(height: 50)
                    .buttonStyle(.bordered)
            }
            Button {
                model.placeAtCrosshair?()
            } label: {
                Label("Setzen", systemImage: "plus.viewfinder")
                    .font(.body.weight(.semibold))
                    .frame(minWidth: 110, minHeight: 50)
            }
            .buttonStyle(.borderedProminent)
        }
        .padding(10)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
    }
}

// MARK: - Crosshair in the centre of the camera view

private struct Crosshair: View {
    let active: Bool

    var body: some View {
        ZStack {
            Circle().stroke(Color.white.opacity(0.9), lineWidth: 1.5).frame(width: 34, height: 34)
            Circle().fill(active ? Theme.accent : Theme.warn).frame(width: 6, height: 6)
            ForEach(0..<4) { i in
                Rectangle().fill(Color.white.opacity(0.9)).frame(width: 1.5, height: 10)
                    .offset(y: -24)
                    .rotationEffect(.degrees(Double(i) * 90))
            }
        }
        .shadow(color: .black.opacity(0.5), radius: 2)
    }
}

// MARK: - Sheets

private struct SpeakerList: View {
    let speakers: [SurveySpeaker]
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List(Array(speakers.enumerated()), id: \.offset) { i, s in
                let p = polar(Vec3(s.position[0], s.position[1], s.position[2]))
                HStack(spacing: 12) {
                    Text("\(i + 1)").font(.system(size: 13, weight: .bold)).frame(width: 26, height: 26).background(Theme.accent, in: Circle())
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Lautsprecher \(i + 1)")
                        Text(String(format: "%.0f° · %.0f° hoch · %.2f m", p.azimuth, p.elevation, p.distance))
                            .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                    }
                    Spacer()
                    if s.confidence >= 0.8 { Image(systemName: "checkmark.circle.fill").foregroundStyle(Theme.ok) }
                }
            }
            .navigationTitle("Gemessen")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Fertig") { dismiss() } } }
        }
    }
}

private struct HelpView: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section("Ablauf") {
                    HelpRow(n: 1, title: "Hörplatz", text: "Den Boden dort antippen, wo das Publikum sitzt. Die Ohrhöhe gilt als Höhe 0, wie in Raumklang.")
                    HelpRow(n: 2, title: "Vorne", text: "Etwas in der Blickrichtung des Publikums antippen. Daraus ergibt sich 0° Azimut.")
                    HelpRow(n: 3, title: "Lautsprecher", text: "Jeden Lautsprecher in der Reihenfolge seiner Ausgänge antippen, mittig auf die Front. Die Richtung, in die er zeigt, misst das iPhone mit.")
                }
                Section("An Raumklang senden") {
                    Text("Oben auf Teilen tippen und per AirDrop an den Mac schicken. In Raumklang die Datei auf das Fenster ziehen oder unter Lautsprecher „iPhone-Messung laden“ wählen.")
                }
                Section("Tipps") {
                    Text("Langsam gehen und den Raum vorher kurz rundherum filmen, dann wird die Messung genauer. Mit LiDAR (iPhone Pro) sind es meist 1 bis 3 cm.")
                    Text("Danach in Raumklang „Mit Mikrofon einmessen“ im Modus „Schnell“: das gleicht Laufzeit und Pegel aus.")
                }
            }
            .navigationTitle("Hilfe")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Fertig") { dismiss() } } }
        }
    }
}

private struct HelpRow: View {
    let n: Int
    let title: String
    let text: String

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Text("\(n)").font(.system(size: 13, weight: .bold)).frame(width: 26, height: 26).background(Theme.accent, in: Circle())
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.headline)
                Text(text).font(.subheadline).foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 2)
    }
}

/// First start: what the app does, in three steps, with page dots, "Überspringen" and "Weiter".
private struct IntroView: View {
    let done: () -> Void
    @State private var page = 0

    private let pages: [(String, String, String)] = [
        ("hifispeaker.2", "Lautsprecher einmessen", "Mit der Kamera und LiDAR misst du, wo jeder Lautsprecher hängt und wohin er zeigt."),
        ("figure.stand", "Hörplatz und Vorne", "Zuerst den Hörplatz und die Richtung nach vorne antippen. Daran richtet sich alles aus."),
        ("square.and.arrow.up", "An Raumklang senden", "Per AirDrop auf den Mac und in Raumklang auf das Fenster ziehen. Fertig."),
    ]

    var body: some View {
        VStack(spacing: 24) {
            TabView(selection: $page) {
                ForEach(pages.indices, id: \.self) { i in
                    VStack(spacing: 18) {
                        Image(systemName: pages[i].0).font(.system(size: 56, weight: .light)).foregroundStyle(Theme.accent)
                        Text(pages[i].1).font(.title2.weight(.semibold))
                        Text(pages[i].2).font(.body).foregroundStyle(.secondary).multilineTextAlignment(.center).padding(.horizontal, 24)
                    }
                    .tag(i)
                }
            }
            .tabViewStyle(.page(indexDisplayMode: .always))
            HStack {
                Button("Überspringen", action: done).buttonStyle(.bordered)
                Spacer()
                Button(page == pages.count - 1 ? "Los geht's" : "Weiter") {
                    if page < pages.count - 1 { withAnimation { page += 1 } } else { done() }
                }
                .buttonStyle(.borderedProminent)
            }
            .padding(.horizontal, 24)
        }
        .padding(.vertical, 32)
        .interactiveDismissDisabled()
    }
}

/// UIKit share sheet (AirDrop, Mail, Files ...).
struct ShareSheet: UIViewControllerRepresentable {
    let items: [Any]
    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: items, applicationActivities: nil)
    }
    func updateUIViewController(_ vc: UIActivityViewController, context: Context) {}
}
