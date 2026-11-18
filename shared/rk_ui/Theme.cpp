#include "Theme.h"

namespace rkui {
namespace {

juce::Colour rgba(int r, int g, int b, float a) { return juce::Colour::fromRGBA((juce::uint8)r, (juce::uint8)g, (juce::uint8)b, (juce::uint8)juce::roundToInt(a * 255.0f)); }

Palette makeDark() {
    Palette p;
    p.bg = juce::Colour(0xff1c1c1e);
    p.side = juce::Colour(0xff232325);
    p.bar = juce::Colour(0xff2a2a2c);
    p.group = rgba(255, 255, 255, 0.045f);
    p.field = rgba(255, 255, 255, 0.08f);
    p.hover = rgba(255, 255, 255, 0.05f);
    p.select = rgba(255, 255, 255, 0.10f);
    p.stage = juce::Colour(0xff111113);
    p.line = rgba(255, 255, 255, 0.08f);
    p.lineStrong = rgba(255, 255, 255, 0.14f);
    p.text = juce::Colour(0xfff2f2f4);
    p.text2 = rgba(235, 235, 245, 0.64f);
    p.text3 = rgba(235, 235, 245, 0.40f);
    p.accent = juce::Colour(0xff0a84ff);
    p.accentSoft = rgba(10, 132, 255, 0.18f);
    p.onAccent = juce::Colours::white;
    p.ok = juce::Colour(0xff32c766);
    p.warn = juce::Colour(0xfff0a531);
    p.bad = juce::Colour(0xffff5a52);
    p.track = rgba(255, 255, 255, 0.16f);
    p.knob = juce::Colour(0xffcdcdd1);
    p.btnFace = rgba(255, 255, 255, 0.11f);
    return p;
}

Palette makeLight() {
    Palette p;
    p.bg = juce::Colour(0xffffffff);
    p.side = juce::Colour(0xfff5f5f7);
    p.bar = juce::Colour(0xffececef);
    p.group = juce::Colour(0xffffffff);
    p.field = rgba(0, 0, 0, 0.05f);
    p.hover = rgba(0, 0, 0, 0.04f);
    p.select = rgba(0, 0, 0, 0.07f);
    p.stage = juce::Colour(0xfff0f0f2);
    p.line = rgba(0, 0, 0, 0.09f);
    p.lineStrong = rgba(0, 0, 0, 0.14f);
    p.text = juce::Colour(0xff1d1d1f);
    p.text2 = rgba(60, 60, 67, 0.78f);
    p.text3 = rgba(60, 60, 67, 0.50f);
    p.accent = juce::Colour(0xff007aff);
    p.accentSoft = rgba(0, 122, 255, 0.12f);
    p.onAccent = juce::Colours::white;
    p.ok = juce::Colour(0xff1f9d4d);
    p.warn = juce::Colour(0xffc27c0e);
    p.bad = juce::Colour(0xffd93a33);
    p.track = rgba(0, 0, 0, 0.12f);
    p.knob = juce::Colour(0xffffffff);
    p.btnFace = juce::Colour(0xffffffff);
    return p;
}

bool g_dark = true;

} // namespace

const Palette& palette() {
    static const Palette dark = makeDark();
    static const Palette light = makeLight();
    return g_dark ? dark : light;
}

bool isDark() { return g_dark; }
void setDarkMode(bool dark) { g_dark = dark; }

juce::Colour itemColour(int index) {
    static const juce::uint32 hues[] = {0xffff5a52, 0xfff0a531, 0xff32c766, 0xff0a84ff,
                                        0xffa879f0, 0xffff6b9a, 0xff3cc3b4, 0xffc9c040};
    return juce::Colour(hues[((index % 8) + 8) % 8]);
}

juce::Font uiFont(float height, bool bold) {
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

void drawNumberBadge(juce::Graphics& g, juce::Rectangle<float> r, int number, juce::Colour c, bool selected) {
    g.setColour(c);
    g.fillEllipse(r);
    if (selected) {
        g.setColour(juce::Colours::white);
        g.drawEllipse(r.reduced(-2.0f), 2.0f);
    }
    g.setColour(juce::Colours::white);
    g.setFont(uiFont(r.getHeight() * 0.58f, true));
    g.drawText(juce::String(number), r, juce::Justification::centred, false);
}

LookAndFeel::LookAndFeel() { refreshColours(); }

void LookAndFeel::refreshColours() {
    const Palette& p = palette();
    setColour(juce::ResizableWindow::backgroundColourId, p.bg);
    setColour(juce::DocumentWindow::backgroundColourId, p.bg);
    setColour(juce::Label::textColourId, p.text);
    setColour(juce::TextButton::buttonColourId, p.btnFace);
    setColour(juce::TextButton::buttonOnColourId, p.accent);
    setColour(juce::TextButton::textColourOffId, p.text);
    setColour(juce::TextButton::textColourOnId, p.onAccent);
    setColour(juce::ToggleButton::textColourId, p.text);
    setColour(juce::ToggleButton::tickColourId, p.onAccent);
    setColour(juce::ComboBox::backgroundColourId, p.field);
    setColour(juce::ComboBox::textColourId, p.text);
    setColour(juce::ComboBox::arrowColourId, p.text2);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::PopupMenu::backgroundColourId, p.bar);
    setColour(juce::PopupMenu::textColourId, p.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    setColour(juce::PopupMenu::highlightedTextColourId, p.onAccent);
    setColour(juce::TextEditor::backgroundColourId, p.field);
    setColour(juce::TextEditor::textColourId, p.text);
    setColour(juce::TextEditor::highlightColourId, p.accentSoft);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, p.accent);
    setColour(juce::CaretComponent::caretColourId, p.accent);
    setColour(juce::Slider::textBoxTextColourId, p.text);
    setColour(juce::Slider::textBoxBackgroundColourId, p.field);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, p.lineStrong);
    setColour(juce::AlertWindow::backgroundColourId, p.bar);
    setColour(juce::AlertWindow::textColourId, p.text);
    setColour(juce::AlertWindow::outlineColourId, p.line);
    setColour(juce::GroupComponent::textColourId, p.text2);
    setColour(juce::GroupComponent::outlineColourId, p.line);
    setColour(juce::TooltipWindow::backgroundColourId, p.bar);
    setColour(juce::TooltipWindow::textColourId, p.text);
    setColour(juce::TooltipWindow::outlineColourId, p.line);
    setColour(juce::HyperlinkButton::textColourId, p.accent);
    getCurrentColourScheme().setUIColour(ColourScheme::windowBackground, p.bg);
    getCurrentColourScheme().setUIColour(ColourScheme::widgetBackground, p.field);
    getCurrentColourScheme().setUIColour(ColourScheme::menuBackground, p.bar);
    getCurrentColourScheme().setUIColour(ColourScheme::outline, p.line);
    getCurrentColourScheme().setUIColour(ColourScheme::defaultText, p.text);
    getCurrentColourScheme().setUIColour(ColourScheme::defaultFill, p.accent);
    getCurrentColourScheme().setUIColour(ColourScheme::highlightedText, p.onAccent);
    getCurrentColourScheme().setUIColour(ColourScheme::highlightedFill, p.accent);
    getCurrentColourScheme().setUIColour(ColourScheme::menuText, p.text);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) {
    const Palette& p = palette();
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState() && b.getClickingTogglesState();
    const bool flat = b.getProperties().getWithDefault("flat", false);
    const bool primary = b.getProperties().getWithDefault("primary", false);
    juce::Colour fill = on || primary ? p.accent : (flat ? juce::Colours::transparentBlack : p.btnFace);
    if (b.getProperties().getWithDefault("segment", false)) fill = on ? p.select.withMultipliedAlpha(1.6f) : juce::Colours::transparentBlack;
    if (highlighted && !on && !primary) fill = fill.isTransparent() ? p.hover : fill.brighter(0.08f);
    if (down) fill = fill.isTransparent() ? p.select : fill.darker(0.12f);
    if (!b.isEnabled()) fill = fill.withMultipliedAlpha(0.5f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 6.0f);
    if (!flat && !on && !primary && !b.getProperties().getWithDefault("segment", false)) {
        g.setColour(p.line);
        g.drawRoundedRectangle(r, 6.0f, 0.75f);
    }
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int h) { return uiFont(juce::jmin(13.5f, h * 0.5f)); }

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    const Palette& p = palette();
    const bool on = (b.getToggleState() && b.getClickingTogglesState()) || (bool)b.getProperties().getWithDefault("primary", false);
    const bool segment = b.getProperties().getWithDefault("segment", false);
    juce::Colour c = (on && !segment) ? p.onAccent : (segment && !b.getToggleState() ? p.text2 : p.text);
    if (b.getProperties().getWithDefault("danger", false)) c = p.bad;
    if (!b.isEnabled()) c = c.withMultipliedAlpha(0.45f);
    g.setColour(c);
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(8, 2), juce::Justification::centred, 1);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down) {
    const Palette& p = palette();
    const float box = 16.0f;
    const float y = (b.getHeight() - box) * 0.5f;
    drawTickBox(g, b, 2.0f, y, box, box, b.getToggleState(), b.isEnabled(), highlighted, down);
    g.setColour(b.isEnabled() ? p.text : p.text3);
    g.setFont(uiFont(13.5f));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().withTrimmedLeft(26), juce::Justification::centredLeft, 2);
}

void LookAndFeel::drawTickBox(juce::Graphics& g, juce::Component&, float x, float y, float w, float h, bool ticked,
                              bool enabled, bool highlighted, bool) {
    const Palette& p = palette();
    juce::Rectangle<float> r(x, y, w, h);
    g.setColour(ticked ? p.accent : (highlighted ? p.select : p.field));
    g.fillRoundedRectangle(r, 4.0f);
    if (!ticked) {
        g.setColour(p.lineStrong);
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    } else {
        juce::Path tick;
        tick.startNewSubPath(r.getX() + w * 0.25f, r.getCentreY());
        tick.lineTo(r.getX() + w * 0.43f, r.getY() + h * 0.70f);
        tick.lineTo(r.getX() + w * 0.76f, r.getY() + h * 0.30f);
        g.setColour(p.onAccent.withMultipliedAlpha(enabled ? 1.0f : 0.5f));
        g.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) {
    const Palette& p = palette();
    auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(0.5f);
    g.setColour(box.isMouseOver(true) ? p.select : p.field);
    g.fillRoundedRectangle(r, 6.0f);
    juce::Path arrow;
    const float cx = (float)w - 14.0f, cy = h * 0.5f;
    arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
    arrow.lineTo(cx, cy + 2.5f);
    arrow.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(p.text2);
    g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&) { return uiFont(13.5f); }
juce::Font LookAndFeel::getLabelFont(juce::Label& l) {
    if (l.getProperties().contains("fontSize")) return uiFont((float)l.getProperties()["fontSize"], l.getProperties().getWithDefault("bold", false));
    return uiFont(13.5f);
}
juce::Font LookAndFeel::getPopupMenuFont() { return uiFont(14.0f); }

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    const Palette& p = palette();
    g.fillAll(p.bar);
    g.setColour(p.lineStrong);
    g.drawRect(0, 0, w, h, 1);
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                   juce::Slider::SliderStyle style, juce::Slider& s) {
    const Palette& p = palette();
    if (style != juce::Slider::LinearHorizontal) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const float cy = y + h * 0.5f;
    const float x0 = (float)x + 8.0f, x1 = (float)(x + w) - 8.0f;
    g.setColour(p.track);
    g.fillRoundedRectangle(x0, cy - 2.0f, x1 - x0, 4.0f, 2.0f);
    // Fill from the "zero" point if the range crosses it, else from the left.
    const double zero = juce::jlimit(s.getMinimum(), s.getMaximum(), static_cast<double>(s.getProperties().getWithDefault("origin", s.getMinimum())));
    const float zx = (float)s.getPositionOfValue(zero);
    g.setColour(s.isEnabled() ? p.accent : p.text3);
    g.fillRoundedRectangle(juce::jmin(zx, pos), cy - 2.0f, std::abs(pos - zx), 4.0f, 2.0f);
    const float r = 8.0f;
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillEllipse(pos - r, cy - r + 1.0f, 2 * r, 2 * r);
    g.setColour(s.isMouseButtonDown() ? p.knob.brighter(0.2f) : p.knob);
    g.fillEllipse(pos - r, cy - r, 2 * r, 2 * r);
}

void LookAndFeel::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor&) {
    g.setColour(palette().field);
    g.fillRoundedRectangle(0, 0, (float)w, (float)h, 6.0f);
}

void LookAndFeel::drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor& e) {
    if (e.hasKeyboardFocus(true) && !e.isReadOnly()) {
        g.setColour(palette().accent);
        g.drawRoundedRectangle(1, 1, (float)w - 2, (float)h - 2, 6.0f, 1.5f);
    }
}

void LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                int thumbStart, int thumbSize, bool over, bool down) {
    const Palette& p = palette();
    juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float>((float)x + w * 0.5f - 2.5f, (float)thumbStart, 5.0f, (float)thumbSize)
                                            : juce::Rectangle<float>((float)thumbStart, (float)y + h * 0.5f - 2.5f, (float)thumbSize, 5.0f);
    g.setColour(over || down ? p.text3 : p.lineStrong);
    g.fillRoundedRectangle(thumb, 2.5f);
}

} // namespace rkui
