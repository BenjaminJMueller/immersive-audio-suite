#include "Pages.h"
#include "ReverbPanel.h"
#include "Visuals.h"

#include "Strings.h"

namespace rkui {
namespace {

bool layoutAdvancedOpen = false;

juce::String dbText(double v) { return v <= -79.9 ? juce::String(juce::CharPointer_UTF8("\xe2\x88\x92\xe2\x88\x9e dB")) : num(v, 1) + " dB"; }

} // namespace

bool describeLayout(const Scene& s, StatusCard& card) {
    const rk::LayoutInfo info = rk::analyze(s.layout);
    auto has = [&info](const char* id) { return std::find(info.hints.begin(), info.hints.end(), id) != info.hints.end(); };
    const juce::String method = juce::String(rk::pannerId(s.effectivePanner())).toUpperCase().replace("AMBISONICS", "Ambisonics");
    const juce::String counts = juce::String(info.numActive) + " " + tr("Lautsprecher") + (info.numSubs > 0 ? " + " + juce::String(info.numSubs) + " Sub" : juce::String());
    const juce::String dot = ", ";
    if (info.numActive == 0) {
        card.set(StatusCard::Kind::Warn, tr("Keine Lautsprecher"), tr("Vorlage wählen oder Lautsprecher hinzufügen."));
    } else if (has("single_speaker")) {
        card.set(StatusCard::Kind::Warn, tr("Nur ein Lautsprecher"), tr("Mit einem Lautsprecher lässt sich keine Richtung darstellen."));
    } else if (has("unequal_distances")) {
        card.set(StatusCard::Kind::Warn, tr("Abstände sehr verschieden"), counts + ", " + method + ". " + tr("„Laufzeit und Pegel ausgleichen“ hilft."));
    } else {
        return false;
    }
    return true;
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
    if (describeLayout(s, *card)) {
        const int cardH = card->idealHeight(juce::jmax(200, page.getWidth() - 24));
        page.add(std::move(card), cardH);
    }

    // Measuring and aligning: one list, the microphone first as the usual way.
    {
        std::vector<ActionList::Action> acts;
        if (ctx.calibrate) acts.push_back({tr("Mit Mikrofon einmessen"), tr("Laufzeit, Pegel und Position messen"), ctx.calibrate});
        if (ctx.importSurvey) acts.push_back({tr("Mit dem iPhone einmessen"), tr("Position und Ausrichtung mit „Raumklang Messen“"), ctx.importSurvey});
        acts.push_back({tr("Laufzeit und Pegel ausgleichen"), tr("Ohne Messung, aus den Abständen im Layout"), [ctx] {
                            alignDistances(ctx.scene->layout);
                            ctx.changed(true);
                            ctx.commit();
                            ctx.rebuild();
                        }});
        page.add(std::make_unique<SectionHeader>(tr("Einmessen"), tr("Mit Mikrofon: Raumklang spielt über jeden Lautsprecher einen kurzen Messton und berechnet daraus Laufzeit, Pegel und, mit mehreren Messpunkten, die genaue Position.\n\nMit dem iPhone: Die App „Raumklang Messen“ misst mit dem LiDAR-Sensor Position und Ausrichtung jedes Lautsprechers. Die nächste Seite erklärt den Ablauf.\n\nLaufzeit und Pegel ausgleichen: Gleicht Laufzeit und Pegel aus, als stünden alle Lautsprecher so weit weg wie der fernste. Das braucht keine Messung, setzt aber ein genaues Layout voraus.")), 26);
        auto list = std::make_unique<ActionList>(std::move(acts));
        const int h = list->idealHeight();
        page.add(std::move(list), h);
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

        // Direction: "Drehung" 0 deg = towards the centre, 180 deg = outwards (inner donut ring);
        // "Neigung" tilts up or down. Presets set both.
        const auto angles = aimAngles(s.layout.speakers[(size_t)sel]);
        auto* aim = page.add(std::make_unique<ChoiceRow>(tr("Ausrichtung"), juce::StringArray{tr("Zur Mitte"), tr("Nach außen"), tr("Eigener Winkel")}), 36);
        {
            const auto& sp = s.layout.speakers[(size_t)sel];
            const int idx = rk::length(sp.aim) < 1e-6f ? 0 : (std::abs(std::abs(angles.first) - 180.0f) < 0.5f && std::abs(angles.second) < 0.5f ? 1 : 2);
            aim->box().setSelectedItemIndex(idx, juce::dontSendNotification);
        }
        aim->onChange = [ctx, sel, toCustom](int i) {
            if (sel >= (int)ctx.scene->layout.speakers.size() || i == 2) return;
            auto& sp = ctx.scene->layout.speakers[(size_t)sel];
            if (i == 0) sp.aim = {};
            else setAimAngles(sp, 180.0f, 0.0f);
            toCustom();
            ctx.changed(true);
            ctx.commit();
            ctx.rebuild();
        };
        const juce::String deg = juce::String(juce::CharPointer_UTF8("\xc2\xb0"));
        auto* rot = page.add(std::make_unique<ParamSlider>(tr("Drehung"), -180, 180, 0.5, deg, 1, 0), 32);
        rot->setTooltip(tr("0° zeigt zur Mitte, 180° nach außen. Positive Werte drehen nach links."));
        rot->setValue(angles.first);
        auto* tilt = page.add(std::make_unique<ParamSlider>(tr("Neigung"), -90, 90, 0.5, deg, 1, 0), 32);
        tilt->setTooltip(tr("Positive Werte neigen den Lautsprecher nach oben."));
        tilt->setValue(angles.second);
        auto applyAim = [ctx, sel, rot, tilt, aim] {
            if (sel >= (int)ctx.scene->layout.speakers.size()) return;
            setAimAngles(ctx.scene->layout.speakers[(size_t)sel], (float)rot->getValue(), (float)tilt->getValue());
            aim->box().setSelectedItemIndex(2, juce::dontSendNotification);
            ctx.changed(true);
        };
        rot->onChange = [applyAim](double) { applyAim(); };
        tilt->onChange = [applyAim](double) { applyAim(); };
        rot->onCommit = [ctx, toCustom] { toCustom(); ctx.commit(); };
        tilt->onCommit = [ctx, toCustom] { toCustom(); ctx.commit(); };
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

    // Rarely needed: the panning method. "Automatisch" picks the right one for the layout.
    page.add(std::make_unique<ExpandRow>(tr("Erweitert"), layoutAdvancedOpen, [ctx] {
        layoutAdvancedOpen = !layoutAdvancedOpen;
        ctx.rebuild();
    }), 30);
    if (layoutAdvancedOpen) {
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
            auto* ro = page.add(std::make_unique<ParamSlider>(tr("Rolloff"), 3, 12, 0.5, "dB", 1, 3), 32);
            ro->setTooltip(tr("Wie stark der Pegel mit doppeltem Abstand abnimmt."));
            ro->setValue(s.dbapRolloffDb);
            ro->onChange = [ctx](double v) { ctx.scene->dbapRolloffDb = (float)v; ctx.changed(true); };
            ro->onCommit = ctx.commit;
            auto* bl = page.add(std::make_unique<ParamSlider>(tr("Spatial Blur"), 0.05, 3, 0.05, "m", 2, 0.05), 32);
            bl->setTooltip(tr("Verhindert, dass eine Quelle genau an einem Lautsprecher nur aus diesem kommt. Größere Werte verteilen sie weicher auf die Nachbarn."));
            bl->setValue(s.dbapBlur);
            bl->onChange = [ctx](double v) { ctx.scene->dbapBlur = (float)v; ctx.changed(true); };
            bl->onCommit = ctx.commit;
            auto* di = page.add(std::make_unique<ParamSlider>(tr("Richtwirkung"), 0, 100, 1, "%", 0, 0), 32);
            di->setTooltip(tr("Lautsprecher, die von der Quelle wegzeigen, tragen weniger bei. Wichtig für den Donut."));
            di->setValue(s.dbapDirectivity * 100.0);
            di->onChange = [ctx](double v) { ctx.scene->dbapDirectivity = (float)v / 100.0f; ctx.changed(true); };
            di->onCommit = ctx.commit;
        }
    }
    page.addDetails(tr("Die Nummer eines Lautsprechers ist sein Ausgang am Audio-Interface. Lautsprecher 1 spielt auf Ausgang 1 und so weiter.\n\n"
                       "Abstand und Azimut zählen vom Hörplatz aus, 0° ist vorne, positive Winkel liegen links.\n\n"
                       "Beim Donut zeigt der Innenring nach außen und der Außenring nach innen. Die Ausrichtung jedes Lautsprechers lässt sich einzeln ändern."));
}

void buildRoomPage(FormPage& page, PageContext ctx) {
    page.clear();
    Scene& s = *ctx.scene;
    // Every change also re-reads the values, because the three reverb times depend on each other.
    auto set = [ctx](std::function<void(Scene&)> f) {
        return [ctx, f](double) { f(*ctx.scene); ctx.changed(false); };
    };

    page.add(std::make_unique<SectionHeader>(tr("Raum"), tr("Der Hall kommt aus allen Lautsprechern, auf jedem dekorreliert, damit er diffus klingt. Wie viel jede Quelle davon abgibt, regelt ihr Hall-Send, in der App unter Quellen oder im Plugin.")), 26);
    // Room type: the same choice as the list on the left. Changing any value below turns it into "Eigener".
    {
        juce::StringArray names;
        for (const auto& rp : roomPresets()) names.add(tr(rp.name));
        const int match = matchRoomPreset(s.reverb, s.reverbWetDb);
        if (match < 0) names.add(tr("Eigener"));
        auto* type = page.add(std::make_unique<ChoiceRow>(tr("Raumtyp"), names), 36);
        type->box().setSelectedItemIndex(match >= 0 ? match : names.size() - 1, juce::dontSendNotification);
        type->onChange = [ctx](int i) {
            const auto& presets = roomPresets();
            if (i < 0 || i >= (int)presets.size()) return;
            ctx.scene->reverb = presets[(size_t)i].params;
            ctx.scene->reverbWetDb = presets[(size_t)i].wetDb;
            ctx.changed(false);
            ctx.commit();
            ctx.rebuild();
        };
    }
    auto* size = page.add(std::make_unique<ParamSlider>(tr("Raumgröße"), 0.3, 2.0, 0.01, juce::String(), 0, 0.3), 32);
    size->formatter = [](double v) { return juce::String(juce::roundToInt(roomVolume((float)v) / 10.0f) * 10) + juce::String::fromUTF8(" m\xc2\xb3"); };
    size->setValue(s.reverb.size);
    size->onChange = set([size](Scene& sc) { sc.reverb.size = (float)size->getValue(); });
    size->onCommit = ctx.commit;
    auto* pre = page.add(std::make_unique<ParamSlider>(tr("Pre-Delay"), 0, 250, 1, "ms", 0, 0), 32);
    pre->setValue(s.reverb.preDelayMs);
    pre->onChange = set([pre](Scene& sc) { sc.reverb.preDelayMs = (float)pre->getValue(); });
    pre->onCommit = ctx.commit;

    page.add(std::make_unique<SectionHeader>(tr("Nachhallzeit"), tr("Wie lange der Raum nachklingt, bis er um 60 dB leiser ist: Tiefen bei 100 Hz, Mitten bei 1 kHz, Höhen bei 8 kHz. Die Höhen klingen höchstens so lange wie die Mitten. Die Punkte lassen sich auch in der Kurve ziehen.")), 26);
    const juce::String bandNames[3] = {tr("Tiefen"), tr("Mitten"), tr("Höhen")};
    for (int b = 0; b < 3; ++b) {
        auto* sl = page.add(std::make_unique<ParamSlider>(bandNames[b], 0.1, 12, 0.05, "s", 1, 0.1), 32);
        sl->setSkewForCentre(2.0);
        sl->setValue(reverbBandRt(s.reverb, b));
        sl->onChange = set([sl, b](Scene& sc) { setReverbBandRt(sc.reverb, b, (float)sl->getValue()); });
        sl->onCommit = [ctx] { ctx.commit(); ctx.rebuild(); };
    }

    page.add(std::make_unique<SectionHeader>(tr("Anteile"), tr("Gesamt regelt den ganzen Hall. Frühe Reflexionen sind die ersten Echos von Wänden und Decke, der Cluster die dichten Reflexionen danach, der Nachhall das lange Ausklingen.")), 26);
    auto addDb = [&page, &set, ctx](const juce::String& label, float value, std::function<void(Scene&, float)> apply) {
        auto* sl = page.add(std::make_unique<ParamSlider>(label, -80, 6, 0.5, "dB", 1, -80), 32);
        sl->setSkewForCentre(-12.0);
        sl->formatter = dbText;
        sl->setValue(value);
        sl->onChange = set([sl, apply](Scene& sc) { apply(sc, (float)sl->getValue()); });
        sl->onCommit = ctx.commit;
    };
    addDb(tr("Gesamt"), s.reverbWetDb, [](Scene& sc, float v) { sc.reverbWetDb = v; });
    addDb(tr("Frühe Reflexionen"), s.reverb.earlyDb, [](Scene& sc, float v) { sc.reverb.earlyDb = v; });
    addDb(tr("Cluster"), s.reverb.clusterDb, [](Scene& sc, float v) { sc.reverb.clusterDb = v; });
    addDb(tr("Nachhall"), s.reverb.tailDb, [](Scene& sc, float v) { sc.reverb.tailDb = v; });

    page.addDetails(tr("Raumtyp oben oder links in der Liste wählen und danach anpassen. Jede Änderung macht daraus einen eigenen Raum.\n\n"
                       "Raumgröße: Abstand der Wände. Größere Räume haben spätere und weiter auseinanderliegende Reflexionen.\n\n"
                       "Pre-Delay: Zeit zwischen dem direkten Klang und den ersten Reflexionen. Größere Werte lassen den Raum größer wirken und halten Sprache verständlich."));
}

/// Speaker direction as (rotation from "towards the centre", tilt), in degrees.
std::pair<float, float> aimAngles(const rk::Speaker& sp) {
    const rk::Vec3 d = sp.aimDirection();
    const rk::Vec3 ref = rk::normalized({-sp.position.x, -sp.position.y, 0.0f}, {0.0f, 1.0f, 0.0f});
    const float tilt = juce::radiansToDegrees(std::asin(juce::jlimit(-1.0f, 1.0f, d.z)));
    const float hx = d.x, hy = d.y;
    if (std::hypot(hx, hy) < 1e-5f) return {0.0f, tilt};
    // Positive rotation = counter-clockwise seen from above (to the left).
    const float rot = juce::radiansToDegrees(std::atan2(ref.x * hy - ref.y * hx, ref.x * hx + ref.y * hy));
    return {rot, tilt};
}

void setAimAngles(rk::Speaker& sp, float rotDeg, float tiltDeg) {
    const rk::Vec3 ref = rk::normalized({-sp.position.x, -sp.position.y, 0.0f}, {0.0f, 1.0f, 0.0f});
    const float a = juce::degreesToRadians(rotDeg), t = juce::degreesToRadians(juce::jlimit(-90.0f, 90.0f, tiltDeg));
    const float hx = ref.x * std::cos(a) - ref.y * std::sin(a), hy = ref.x * std::sin(a) + ref.y * std::cos(a);
    sp.aim = {hx * std::cos(t), hy * std::cos(t), std::sin(t)};
}

} // namespace rkui
