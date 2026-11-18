// Raumklang UI: colours, fonts and the LookAndFeel.
// The palette mirrors Projection Studio (src/renderer/styles.css) so that all apps
// of the family look and feel the same: macOS-like, dark by default, light on request.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace rkui {

struct Palette {
    juce::Colour bg, side, bar, group, field, hover, select, stage, line, lineStrong;
    juce::Colour text, text2, text3, accent, accentSoft, onAccent, ok, warn, bad;
    juce::Colour track, knob, btnFace;
};

const Palette& palette();
bool isDark();
/// Follows the operating system unless forced.
void setDarkMode(bool dark);

/// Colour of numbered item circles (sources, speaker groups), cycling through 8 hues.
juce::Colour itemColour(int index);

juce::Font uiFont(float height, bool bold = false);

/// Draws a numbered circle like in Projection Studio's lists and stage.
void drawNumberBadge(juce::Graphics& g, juce::Rectangle<float> r, int number, juce::Colour c, bool selected = false);

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void refreshColours();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawTickBox(juce::Graphics&, juce::Component&, float x, float y, float w, float h, bool ticked,
                     bool enabled, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh,
                      juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius(juce::Slider&) override { return 8; }
    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                       int thumbSize, bool over, bool down) override;
};

} // namespace rkui
