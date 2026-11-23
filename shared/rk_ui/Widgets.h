// Raumklang UI: reusable building blocks in the Projection Studio style.
#pragma once

#include "Theme.h"

#include <functional>
#include <vector>

namespace rkui {

/// Section title inside the inspector ("Punkte", "Lautsprecher", ...). With an info text
/// it shows a small (i) whose tooltip holds the longer explanation, so the form stays short.
class SectionHeader : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit SectionHeader(juce::String text, juce::String info = {});
    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    juce::String text_, info_;
};

/// Square tool button with a line icon (toolbar: undo, redo, open, save, help).
class IconButton : public juce::Button {
public:
    IconButton(Icon icon, const juce::String& tooltip);
    void setIcon(Icon i) { icon_ = i; repaint(); }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    Icon icon_;
};

/// "Details und Hilfen": a collapsible block of explanatory text at the end of a page.
class Disclosure : public juce::Component {
public:
    Disclosure(juce::String title, juce::String text);
    /// Height for the given width in the current state.
    int heightFor(int width) const;
    std::function<void()> onToggle;
    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    juce::String title_, text_;
};

/// "› Erweitert": a row that opens and closes the rarely needed settings below it.
/// The toggle runs asynchronously, so it may rebuild the page that owns this row.
class ExpandRow : public juce::Component {
public:
    ExpandRow(juce::String title, bool open, std::function<void()> toggle);
    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override;

private:
    juce::String title_;
    bool open_;
    std::function<void()> toggle_;
};

/// A short explanation in grey under a setting, wrapped to the page width.
class NoteRow : public juce::Component {
public:
    explicit NoteRow(juce::String text) : text_(std::move(text)) {}
    int heightFor(int width) const;
    void paint(juce::Graphics& g) override;

private:
    juce::String text_;
};

/// Label | slider | value. Calls onChange while dragging and onCommit once at the end
/// (the place to record an undo step).
class ParamSlider : public juce::Component, public juce::SettableTooltipClient {
public:
    ParamSlider(juce::String label, double min, double max, double step, juce::String unit, int decimals,
                double origin = 0.0);
    void setValue(double v, juce::NotificationType n = juce::dontSendNotification);
    double getValue() const { return slider_.getValue(); }
    void setSkewForCentre(double centre) { slider_.setSkewFactorFromMidPoint(centre); }
    void setEnabledState(bool on) { slider_.setEnabled(on); }
    std::function<void(double)> onChange;
    std::function<void()> onCommit;
    /// Optional custom value text, e.g. "−∞ dB".
    std::function<juce::String(double)> formatter;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::String label_, unit_;
    int decimals_;
    juce::Slider slider_;
    juce::Label value_;
    void updateText();
};

/// A labelled combo box row.
class ChoiceRow : public juce::Component, public juce::SettableTooltipClient {
public:
    ChoiceRow(juce::String label, const juce::StringArray& items);
    juce::ComboBox& box() { return box_; }
    std::function<void(int)> onChange; // 0-based index
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::String label_;
    juce::ComboBox box_;
};

/// A labelled text field.
class TextRow : public juce::Component {
public:
    explicit TextRow(juce::String label);
    juce::TextEditor& editor() { return editor_; }
    std::function<void(const juce::String&)> onCommit;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::String label_;
    juce::TextEditor editor_;
};

/// Horizontal segmented control (also used for the numbered workflow steps).
class Segmented : public juce::Component, public juce::SettableTooltipClient {
public:
    Segmented(const juce::StringArray& items, bool numbered);
    void setSelected(int index);
    void setItems(const juce::StringArray& items) { items_ = items; repaint(); }
    int selected() const { return selected_; }
    std::function<void(int)> onChange;
    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    int idealWidth() const;

private:
    int indexAt(juce::Point<int> p) const;
    std::vector<juce::Rectangle<int>> segmentBounds() const;
    juce::StringArray items_;
    bool numbered_;
    int selected_ = 0;
    int hover_ = -1;
};

/// A one-line hint in a plugin's top bar, right-aligned at `rightX`: orange when something
/// needs fixing, grey for plain information. Draws nothing if it does not fit.
void drawNoticePill(juce::Graphics& g, float leftLimit, float rightX, float centreY, bool warn, const juce::String& text);

/// The coach bar at the bottom of a view: a numbered badge (or a tick when `ok`), a short
/// title and one line of text. Used by the room view and the reverb editor.
void drawCoachBar(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& badge, const juce::String& title,
                  const juce::String& text, bool ok);

/// Green/orange card with a plain-language verdict ("Positionen bestimmt").
class StatusCard : public juce::Component {
public:
    enum class Kind { Ok, Warn, Info };
    void set(Kind k, juce::String title, juce::String detail);
    /// Optional segmented bar under the text (e.g. 5 of 6 outputs connected). total 0 hides it.
    void setProgress(int filled, int total) { filled_ = filled; total_ = total; repaint(); }
    void paint(juce::Graphics& g) override;
    int idealHeight(int width) const;

private:
    Kind kind_ = Kind::Info;
    juce::String title_, detail_;
    int filled_ = 0, total_ = 0;
};

/// Sidebar list with group headers and numbered badges (like the projector list).
class SidebarList : public juce::Component {
public:
    struct Row {
        juce::String group;
        int number = 0;
        juce::Colour colour;
        juce::String title, subtitle, trailing;
        bool dim = false;
        bool check = false;
        float level = 0.0f; // 0..1, drawn as a thin meter
        bool toggles = false; // show M (mute) and S (solo) buttons on the right
        bool mute = false, solo = false;
    };
    void setRows(std::vector<Row> rows);
    void setSelected(int row);
    int selected() const { return selected_; }
    void setLevels(const std::vector<float>& levels);
    std::function<void(int)> onSelect;
    /// A click on a row's M (which = 0) or S (which = 1) button.
    std::function<void(int row, int which)> onToggle;
    int contentHeight() const;
    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    struct Item { int row; juce::Rectangle<int> bounds; };
    static juce::Rectangle<float> toggleRect(juce::Rectangle<float> row, int which);
    int hover_ = -1;
    std::vector<Item> layoutItems() const;
    std::vector<Row> rows_;
    int selected_ = -1;
};

/// The output, pinned at the bottom of the sidebar: overall level as a fader with the
/// summed output meter under it. Double click resets to 0 dB.
class OutputStrip : public juce::Component, public juce::SettableTooltipClient {
public:
    OutputStrip();
    void setGainDb(float db);
    void setLevel(float unit); // 0..1
    std::function<void(float)> onChange;
    std::function<void()> onCommit;
    static constexpr float kMinDb = -60.0f, kMaxDb = 12.0f;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<float> track() const;
    float unitForDb(float db) const;
    float dbForX(float x) const;
    float gainDb_ = 0.0f, level_ = 0.0f;
    bool dragging_ = false;
};

/// A vertical stack of rows inside a viewport: the inspector. Consecutive setting rows
/// (sliders, choices, text fields, switches) are drawn together on a rounded card with
/// hairline separators, like a macOS settings group. Buttons sit right-aligned at their
/// natural width unless they carry the property "block".
class FormPage : public juce::Component {
public:
    FormPage();
    void clear();
    template <typename T>
    T* add(std::unique_ptr<T> c, int height) {
        T* raw = c.get();
        addRow(std::move(c), height);
        return raw;
    }
    void addSpace(int height);
    /// Adds the "Details und Hilfen" block at the end of the page.
    void addDetails(const juce::String& text);
    void setRowHeight(juce::Component* c, int height);
    void relayout();
    void resized() override;

private:
    void addRow(std::unique_ptr<juce::Component> c, int height);
    struct Entry { std::unique_ptr<juce::Component> comp; int height; };
    struct Content : juce::Component {
        std::vector<juce::Rectangle<float>> cards;
        std::vector<juce::Line<float>> separators;
        void paint(juce::Graphics& g) override;
    };
    std::vector<Entry> entries_;
    Content content_;
    int buttonRowY_ = -1, buttonRowRight_ = 0;
    juce::Viewport viewport_;
};

/// Small helper for buttons in the toolbar style.
std::unique_ptr<juce::TextButton> makeButton(const juce::String& text, bool primary = false, bool flat = false);

} // namespace rkui
