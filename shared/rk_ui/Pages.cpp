#include "Pages.h"
#include "Visuals.h"

#include "Strings.h"

namespace rkui {
namespace {

juce::String dbText(double v) { return v <= -79.9 ? juce::String(juce::CharPointer_UTF8("\xe2\x88\x92\xe2\x88\x9e dB")) : num(v, 1) + " dB"; }

} // namespace

void describeLayout(const Scene& s, StatusCard& card) {
    const rk::LayoutInfo info = rk::analyze(s.layout);
    auto has = [&info](const char* id) { return std::find(info.hints.begin(), info.hints.end(), id) != info.hints.end(); };
    const juce::String method = juce::String(rk::pannerId(s.effectivePanner())).toUpperCase().replace("AMBISONICS", "Ambisonics");
    const juce::String counts = juce::String(info.numActive) + " " + tr("Lautsprecher") + (info.numSubs > 0 ? " + " + juce::String(info.numSubs) + " Sub" : juce::String());
    const juce::String dot = u8(" \xc2\xb7 ");
    if (info.numActive == 0) {
        card.set(StatusCard::Kind::Warn, tr("Keine Lautsprecher"), tr("Vorlage wählen oder Lautsprecher hinzufügen."));
    } else if (has("single_speaker")) {
        card.set(StatusCard::Kind::Warn, tr("Bitte prüfen"), tr("Nur ein Lautsprecher, also keine Richtung."));
    } else if (has("donut_uses_dbap")) {
        card.set(StatusCard::Kind::Ok, tr("Sitzt gut") + dot + method, counts + ", " + tr("Donut erkannt"));
    } else if (has("unequal_distances")) {
        card.set(StatusCard::Kind::Warn, tr("Bitte prüfen") + dot + method, counts + ", " + tr("Abstände sehr verschieden. „Abstände ausgleichen“ hilft."));
    } else if (has("listener_not_enclosed")) {
        card.set(StatusCard::Kind::Info, tr("Sitzt gut") + dot + method, counts + ", " + tr("Hörplatz nicht umschlossen"));
    } else {
        card.set(StatusCard::Kind::Ok, tr("Sitzt gut") + dot + method, counts + " " + tr("rund um den Hörplatz"));
    }
}

void buildLayoutPage(FormPage& page, PageContext ctx, int sel, std::function<void(int)> selectSpeaker) {
    page.clear();
    Scene& s = *ctx.scene;

    page.add(std::make_unique<SectionHeader>(tr("Aufbau"), tr("Vorlage wählen und Lautsprecher in der Ansicht ziehen. Sobald ein Lautsprecher verschoben ist, wird daraus ein eigenes Layout.")), 26);
    juce::StringArray names;
    for (const auto& id : presetIds()) names.add(tr(presetName(id)));
    auto* preset = page.add(std::make_unique<ChoiceRow>(tr("Vorlage"), names), 36);
    preset->box().setSelectedItemIndex(juce::jmax(0, presetIds().indexOf(s.presetId)), juce::dontSendNotification);
    preset->onChange = [ctx](int i) {
        ctx.scene->presetId = presetIds()[i];
        if (regenerateLayout(*ctx.scene)) {
            ctx.changed(true);
            ctx.commit();
        }
        ctx.rebuild();
    };

    auto regenerate = [ctx] {
        if (regenerateLayout(*ctx.scene)) ctx.changed(true);
    };
    auto commitRebuild = [ctx] { ctx.commit(); ctx.rebuild(); };

    if (s.presetId == "donut") {
        auto* ic = page.add(std::make_unique<ParamSlider>(tr("Anzahl innen"), 1, 32, 1, "", 0, 1), 32);
        ic->setValue(s.innerCount);
        ic->onChange = [ctx, regenerate](double v) { ctx.scene->innerCount = (int)v; regenerate(); };
        ic->onCommit = commitRebuild;
        auto* ir = page.add(std::make_unique<ParamSlider>(tr("Radius innen"), 0.2, 30, 0.1, "m", 1, 0.2), 32);
        ir->setValue(s.innerRadius);
        ir->onChange = [ctx, regenerate](double v) {
            ctx.scene->innerRadius = (float)v;
            ctx.scene->outerRadius = std::max(ctx.scene->outerRadius, ctx.scene->innerRadius + 0.5f);
            regenerate();
        };
        ir->onCommit = commitRebuild;
        auto* oc = page.add(std::make_unique<ParamSlider>(tr("Anzahl außen"), 1, 64, 1, "", 0, 1), 32);
        oc->setValue(s.outerCount);
        oc->onChange = [ctx, regenerate](double v) { ctx.scene->outerCount = (int)v; regenerate(); };
        oc->onCommit = commitRebuild;
        auto* orad = page.add(std::make_unique<ParamSlider>(tr("Radius außen"), 0.7, 60, 0.1, "m", 1, 0.7), 32);
        orad->setValue(s.outerRadius);
        orad->onChange = [ctx, regenerate](double v) {
            ctx.scene->outerRadius = std::max((float)v, ctx.scene->innerRadius + 0.5f);
            regenerate();
        };
        orad->onCommit = commitRebuild;
    } else if (s.presetId == "ring") {
        auto* c = page.add(std::make_unique<ParamSlider>(tr("Anzahl"), 2, 64, 1, "", 0, 2), 32);
        c->setValue(s.ringCount);
        c->onChange = [ctx, regenerate](double v) { ctx.scene->ringCount = (int)v; regenerate(); };
        c->onCommit = commitRebuild;
    }
    if (s.presetId != "custom" && s.presetId != "donut") {
        auto* r = page.add(std::make_unique<ParamSlider>(tr("Radius"), 0.5, 50, 0.1, "m", 1, 0.5), 32);
        r->setSkewForCentre(5.0);
        r->setValue(s.radius);
        r->onChange = [ctx, regenerate](double v) { ctx.scene->radius = (float)v; regenerate(); };
        r->onCommit = commitRebuild;
    }
    if (s.presetId == "ring" || s.presetId == "donut") {
        auto* h = page.add(std::make_unique<ParamSlider>(tr("Höhe"), -5, 20, 0.1, "m", 1, 0), 32);
        h->setValue(s.height);
        h->onChange = [ctx, regenerate](double v) { ctx.scene->height = (float)v; regenerate(); };
        h->onCommit = commitRebuild;
    }

    auto card = std::make_unique<StatusCard>();
    describeLayout(s, *card);
    const int cardH = card->idealHeight(juce::jmax(200, page.getWidth() - 24));
    page.add(std::move(card), cardH);

    if (ctx.calibrate || ctx.importSurvey) {
        page.add(std::make_unique<SectionHeader>(tr("Einmessen"), tr("Mit Mikrofon: Raumklang spielt über jeden Lautsprecher einen kurzen Messton und berechnet daraus Laufzeit, Pegel und, mit mehreren Messpunkten, die genaue Position.\n\nMit dem iPhone: In der App „Raumklang Messen“ jeden Lautsprecher antippen und die Messung hier laden oder auf das Fenster ziehen.")), 26);
        if (ctx.calibrate) {
            auto* mic = page.add(makeButton(tr("Mit Mikrofon einmessen"), true), 32);
            mic->setTooltip(tr("Laufzeit, Pegel und Position jedes Lautsprechers automatisch messen."));
            mic->onClick = ctx.calibrate;
        }
        if (ctx.importSurvey) {
            auto* phone = page.add(makeButton(tr("iPhone-Messung laden")), 32);
            phone->setTooltip(tr("Datei aus der App „Raumklang Messen“ laden. Sie lässt sich auch auf das Fenster ziehen."));
            phone->onClick = ctx.importSurvey;
        }
    }

    page.add(std::make_unique<SectionHeader>(tr("Verfahren"), tr("VBAP für Lautsprecher rund um den Hörplatz, DBAP für Aufbauten ohne Mitte wie den Donut, Ambisonics für gleichmäßige Kuppeln. „Automatisch“ wählt passend zum Layout.")), 26);
    auto* method = page.add(std::make_unique<Segmented>(juce::StringArray{tr("Automatisch"), "VBAP", "DBAP", "Ambisonics"}, false), 32);
    method->setSelected(s.autoPanner ? 0 : 1 + static_cast<int>(s.panner));
    method->onChange = [ctx](int i) {
        ctx.scene->autoPanner = i == 0;
        if (i > 0) ctx.scene->panner = static_cast<rk::PannerKind>(i - 1);
        ctx.changed(true);
        ctx.commit();
        ctx.rebuild();
    };
    if (s.effectivePanner() == rk::PannerKind::DBAP) {
        auto* ro = page.add(std::make_unique<ParamSlider>(tr("Abfall"), 3, 12, 0.5, "dB", 1, 3), 32);
        ro->setTooltip(tr("Wie stark der Pegel mit doppeltem Abstand abnimmt."));
        ro->setValue(s.dbapRolloffDb);
        ro->onChange = [ctx](double v) { ctx.scene->dbapRolloffDb = (float)v; ctx.changed(true); };
        ro->onCommit = ctx.commit;
        auto* bl = page.add(std::make_unique<ParamSlider>(tr("Unschärfe"), 0.05, 3, 0.05, "m", 2, 0.05), 32);
        bl->setTooltip(tr("Verhindert, dass eine Quelle direkt an einem Lautsprecher zu laut wird."));
        bl->setValue(s.dbapBlur);
        bl->onChange = [ctx](double v) { ctx.scene->dbapBlur = (float)v; ctx.changed(true); };
        bl->onCommit = ctx.commit;
        auto* di = page.add(std::make_unique<ParamSlider>(tr("Richtwirkung"), 0, 100, 1, "%", 0, 0), 32);
        di->setTooltip(tr("Lautsprecher, die von der Quelle wegzeigen, tragen weniger bei. Wichtig für den Donut."));
        di->setValue(s.dbapDirectivity * 100.0);
        di->onChange = [ctx](double v) { ctx.scene->dbapDirectivity = (float)v / 100.0f; ctx.changed(true); };
        di->onCommit = ctx.commit;
    }

    // Selected speaker.
    if (sel >= 0 && sel < (int)s.layout.speakers.size()) {
        page.add(std::make_unique<SectionHeader>(tr("Lautsprecher") + " " + juce::String(sel + 1)), 26);
        auto toCustom = [ctx] { ctx.scene->presetId = "custom"; };
        auto* name = page.add(std::make_unique<TextRow>(tr("Name")), 34);
        name->editor().setText(juce::String(s.layout.speakers[(size_t)sel].name), false);
        name->onCommit = [ctx, sel](const juce::String& t) {
            if (sel >= (int)ctx.scene->layout.speakers.size()) return;
            auto& n = ctx.scene->layout.speakers[(size_t)sel].name;
            if (juce::String(n) == t) return;
            n = t.substring(0, 64).toStdString();
            ctx.changed(false);
            ctx.commit();
        };
        const rk::Polar pol = rk::toPolar(s.layout.speakers[(size_t)sel].position);
        auto setPolar = [ctx, sel, toCustom](int which, double v) {
            if (sel >= (int)ctx.scene->layout.speakers.size()) return;
            auto& sp = ctx.scene->layout.speakers[(size_t)sel];
            rk::Polar p = rk::toPolar(sp.position);
            if (p.distance < 0.01f) p.distance = 1.0f;
            if (which == 0) p.azimuthDeg = (float)v;
            if (which == 1) p.elevationDeg = (float)v;
            if (which == 2) p.distance = (float)v;
            sp.position = rk::toCartesian(p);
            toCustom();
            ctx.changed(true);
        };
        auto* az = page.add(std::make_unique<ParamSlider>(tr("Azimut"), -180, 180, 0.5, juce::String(juce::CharPointer_UTF8("\xc2\xb0")), 1, 0), 32);
        az->setValue(pol.azimuthDeg);
        az->onChange = [setPolar](double v) { setPolar(0, v); };
        az->onCommit = ctx.commit;
        auto* el = page.add(std::make_unique<ParamSlider>(tr("Elevation"), -90, 90, 0.5, juce::String(juce::CharPointer_UTF8("\xc2\xb0")), 1, 0), 32);
        el->setValue(pol.elevationDeg);
        el->onChange = [setPolar](double v) { setPolar(1, v); };
        el->onCommit = ctx.commit;
        auto* di = page.add(std::make_unique<ParamSlider>(tr("Abstand"), 0.3, 60, 0.05, "m", 2, 0.3), 32);
        di->setSkewForCentre(5.0);
        di->setValue(pol.distance);
        di->onChange = [setPolar](double v) { setPolar(2, v); };
        di->onCommit = ctx.commit;

        auto* aim = page.add(std::make_unique<ChoiceRow>(tr("Ausrichtung"), juce::StringArray{tr("Zur Mitte"), tr("Nach außen"), tr("Nach innen")}), 36);
        {
            const auto& sp = s.layout.speakers[(size_t)sel];
            int idx = 0;
            if (rk::length(sp.aim) > 1e-6f) {
                const rk::Vec3 radial = rk::normalized({sp.position.x, sp.position.y, 0.0f});
                idx = rk::dot(sp.aimDirection(), radial) > 0 ? 1 : 2;
            }
            aim->box().setSelectedItemIndex(idx, juce::dontSendNotification);
        }
        aim->onChange = [ctx, sel, toCustom](int i) {
            if (sel >= (int)ctx.scene->layout.speakers.size()) return;
            auto& sp = ctx.scene->layout.speakers[(size_t)sel];
            const rk::Vec3 radial = rk::normalized({sp.position.x, sp.position.y, 0.0f});
            sp.aim = i == 0 ? rk::Vec3{} : (i == 1 ? radial : -radial);
            toCustom();
            ctx.changed(true);
            ctx.commit();
        };
        auto* gain = page.add(std::make_unique<ParamSlider>(tr("Pegel"), -40, 12, 0.1, "dB", 1, 0), 32);
        gain->setValue(s.layout.speakers[(size_t)sel].gainDb);
        gain->onChange = [ctx, sel](double v) {
            if (sel < (int)ctx.scene->layout.speakers.size()) ctx.scene->layout.speakers[(size_t)sel].gainDb = (float)v;
            ctx.changed(true);
        };
        gain->onCommit = ctx.commit;
        auto* delay = page.add(std::make_unique<ParamSlider>(tr("Verzögerung"), 0, 100, 0.01, "ms", 2, 0), 32);
        delay->setSkewForCentre(10.0);
        delay->setValue(s.layout.speakers[(size_t)sel].delayMs);
        delay->onChange = [ctx, sel](double v) {
            if (sel < (int)ctx.scene->layout.speakers.size()) ctx.scene->layout.speakers[(size_t)sel].delayMs = (float)v;
            ctx.changed(true);
        };
        delay->onCommit = ctx.commit;

        auto* sub = page.add(std::make_unique<juce::ToggleButton>(tr("Subwoofer")), 28);
        sub->setToggleState(s.layout.speakers[(size_t)sel].isSubwoofer, juce::dontSendNotification);
        sub->onClick = [ctx, sel, sub, toCustom] {
            if (sel >= (int)ctx.scene->layout.speakers.size()) return;
            ctx.scene->layout.speakers[(size_t)sel].isSubwoofer = sub->getToggleState();
            toCustom();
            ctx.changed(true);
            ctx.commit();
        };
        auto* act = page.add(std::make_unique<juce::ToggleButton>(tr("Aktiv")), 28);
        act->setToggleState(s.layout.speakers[(size_t)sel].enabled, juce::dontSendNotification);
        act->onClick = [ctx, sel, act] {
            if (sel >= (int)ctx.scene->layout.speakers.size()) return;
            ctx.scene->layout.speakers[(size_t)sel].enabled = act->getToggleState();
            ctx.changed(true);
            ctx.commit();
        };
        auto* rm = page.add(makeButton(tr("Entfernen"), false, true), 30);
        rm->getProperties().set("danger", true);
        rm->onClick = [ctx, sel, selectSpeaker] {
            auto& v = ctx.scene->layout.speakers;
            if (sel >= (int)v.size()) return;
            v.erase(v.begin() + sel);
            ctx.scene->presetId = "custom";
            ctx.changed(true);
            ctx.commit();
            selectSpeaker(juce::jmin(sel, (int)v.size() - 1));
        };
    }

    auto* add = page.add(makeButton("+ " + tr("Lautsprecher")), 32);
    add->setTooltip(tr("Lautsprecher hinzufügen"));
    add->onClick = [ctx, selectSpeaker] {
        auto& v = ctx.scene->layout.speakers;
        if ((int)v.size() >= rk::kMaxSpeakers) return;
        rk::Speaker sp;
        sp.name = std::to_string(v.size() + 1);
        sp.position = rk::toCartesian({0.0f, 0.0f, ctx.scene->radius});
        v.push_back(sp);
        ctx.scene->presetId = "custom";
        ctx.changed(true);
        ctx.commit();
        selectSpeaker((int)v.size() - 1);
    };
    auto* align = page.add(makeButton(tr("Abstände ausgleichen")), 32);
    // Order matters: buttons fill a row from the right, so the main action ends up rightmost.
    align->setTooltip(tr("Gleicht Laufzeit und Pegel aus, als stünden alle Lautsprecher so weit weg wie der fernste."));
    align->onClick = [ctx] {
        alignDistances(ctx.scene->layout);
        ctx.changed(true);
        ctx.commit();
        ctx.rebuild();
    };
    page.addDetails(tr("Die Nummer eines Lautsprechers ist sein Ausgang am Audio-Interface. Lautsprecher 1 spielt auf Ausgang 1 und so weiter.\n\n"
                       "Abstand und Azimut zählen vom Hörplatz aus, 0° ist vorne, positive Winkel liegen links.\n\n"
                       "Beim Donut zeigt der Innenring nach außen und der Außenring nach innen. Die Ausrichtung jedes Lautsprechers lässt sich einzeln ändern."));
}

void buildRoomPage(FormPage& page, PageContext ctx) {
    page.clear();
    Scene& s = *ctx.scene;
    auto* view = page.add(std::make_unique<ReverbView>(), 170);
    view->setTooltip(tr("Farbig: was die Lautsprecher gerade spielen, tief links, hoch rechts. Die Linie zeigt, wie lange der Hall je Tonhöhe nachklingt."));
    view->setParams(s.reverb, s.reverbWetDb);
    if (ctx.reverbView) ctx.reverbView(view);
    ctx.changed = [orig = ctx.changed, view, sc = ctx.scene](bool structural) {
        if (orig) orig(structural);
        view->setParams(sc->reverb, sc->reverbWetDb);
    };
    page.add(std::make_unique<SectionHeader>(tr("Raum"), tr("Der Hall kommt aus allen Lautsprechern, jeweils leicht verschieden. Wie viel jede Quelle abgibt, steht unter Quellen beim Hallanteil.")), 26);
    auto* preset = page.add(std::make_unique<Segmented>(juce::StringArray{tr("Trocken"), tr("Studio"), tr("Saal"), tr("Kirche")}, false), 32);
    preset->setSelected(s.reverbWetDb <= -79.0f ? 0 : (s.reverb.decaySec < 1.0f ? 1 : (s.reverb.decaySec < 3.0f ? 2 : 3)));
    preset->onChange = [ctx](int i) {
        Scene& sc = *ctx.scene;
        switch (i) {
        case 0: sc.reverbWetDb = -80.0f; break;
        case 1: sc.reverb = {0.6f, 9000.0f, 5.0f, 0.5f}; sc.reverbWetDb = -10.0f; break;
        case 2: sc.reverb = {2.2f, 6000.0f, 20.0f, 1.2f}; sc.reverbWetDb = -6.0f; break;
        default: sc.reverb = {5.5f, 4000.0f, 35.0f, 2.0f}; sc.reverbWetDb = -4.0f; break;
        }
        ctx.changed(false);
        ctx.commit();
        ctx.rebuild();
    };
    page.add(std::make_unique<SectionHeader>(tr("Hall")), 26);
    auto* decay = page.add(std::make_unique<ParamSlider>(tr("Nachhallzeit"), 0.2, 12, 0.05, "s", 2, 0.2), 32);
    decay->setSkewForCentre(2.0);
    decay->setValue(s.reverb.decaySec);
    decay->onChange = [ctx](double v) { ctx.scene->reverb.decaySec = (float)v; ctx.changed(false); };
    decay->onCommit = ctx.commit;
    auto* size = page.add(std::make_unique<ParamSlider>(tr("Raumgröße"), 30, 200, 1, "%", 0, 30), 32);
    size->setValue(s.reverb.size * 100.0);
    size->onChange = [ctx](double v) { ctx.scene->reverb.size = (float)v / 100.0f; ctx.changed(false); };
    size->onCommit = ctx.commit;
    auto* damp = page.add(std::make_unique<ParamSlider>(tr("Höhendämpfung"), 1000, 20000, 10, "Hz", 0, 20000), 32);
    damp->setSkewForCentre(5000.0);
    damp->setValue(s.reverb.dampingHz);
    damp->formatter = [](double v) { return v >= 1000 ? num(v / 1000.0, 1) + " kHz" : num(v, 0) + " Hz"; };
    damp->setValue(s.reverb.dampingHz);
    damp->onChange = [ctx](double v) { ctx.scene->reverb.dampingHz = (float)v; ctx.changed(false); };
    damp->onCommit = ctx.commit;
    auto* pre = page.add(std::make_unique<ParamSlider>(tr("Vorverzögerung"), 0, 250, 1, "ms", 0, 0), 32);
    pre->setValue(s.reverb.preDelayMs);
    pre->onChange = [ctx](double v) { ctx.scene->reverb.preDelayMs = (float)v; ctx.changed(false); };
    pre->onCommit = ctx.commit;
    auto* wet = page.add(std::make_unique<ParamSlider>(tr("Hallpegel"), -80, 6, 0.5, "dB", 1, -80), 32);
    wet->setSkewForCentre(-12.0);
    wet->formatter = dbText;
    wet->setValue(s.reverbWetDb);
    wet->onChange = [ctx](double v) { ctx.scene->reverbWetDb = (float)v; ctx.changed(false); };
    wet->onCommit = ctx.commit;
    page.addDetails(tr("Nachhallzeit: wie lange der Hall braucht, um um 60 dB abzuklingen.\n\n"
                       "Höhendämpfung: oberhalb dieser Frequenz klingt der Hall schneller ab, wie in einem Raum mit Vorhängen und Publikum.\n\n"
                       "Vorverzögerung: Zeit zwischen dem direkten Klang und dem Einsetzen des Halls. Größere Werte lassen den Raum größer wirken."));
}

} // namespace rkui
