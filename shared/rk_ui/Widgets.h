// Raumklang UI: reusable building blocks in the Projection Studio style.
#pragma once

#include "Theme.h"

#include <functional>
#include <vector>

namespace rkui {

/// Section title inside the inspector ("Punkte", "Lautsprecher", ...).
class SectionHeader : public juce::Component {
public:
    explicit SectionHeader(juce::String text) : text_(std::move(text)) {}
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
class ChoiceRow : public juce::Component {
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
class Segmented : public juce::Component {
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

/// Green/orange card with a plain-language verdict ("Sitzt gut").
class StatusCard : public juce::Component {
public:
    enum class Kind { Ok, Warn, Info };
    void set(Kind k, juce::String title, juce::String detail);
    void paint(juce::Graphics& g) override;
    int idealHeight(int width) const;

private:
    Kind kind_ = Kind::Info;
    juce::String title_, detail_;
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
    };
    void setRows(std::vector<Row> rows);
    void setSelected(int row);
    int selected() const { return selected_; }
    void setLevels(const std::vector<float>& levels);
    std::function<void(int)> onSelect;
    int contentHeight() const;
    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    struct Item { int row; juce::Rectangle<int> bounds; };
    std::vector<Item> layoutItems() const;
    std::vector<Row> rows_;
    int selected_ = -1;
};

/// A vertical stack of rows inside a viewport: the inspector.
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
    void relayout();
    void resized() override;

private:
    void addRow(std::unique_ptr<juce::Component> c, int height);
    struct Entry { std::unique_ptr<juce::Component> comp; int height; };
    std::vector<Entry> entries_;
    juce::Component content_;
    juce::Viewport viewport_;
};

/// Small helper for buttons in the toolbar style.
std::unique_ptr<juce::TextButton> makeButton(const juce::String& text, bool primary = false, bool flat = false);

} // namespace rkui
