#include "Widgets.h"

#include "Strings.h"

#include <map>

namespace rkui {

// ---------------------------------------------------------------------------
SectionHeader::SectionHeader(juce::String text, juce::String info) : text_(std::move(text)), info_(std::move(info)) {
    if (info_.isNotEmpty()) setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void SectionHeader::appendInfo(const juce::String& more) {
    if (more.isEmpty()) return;
    info_ = info_.isEmpty() ? more : info_ + "\n\n" + more;
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    repaint();
}

juce::Rectangle<float> SectionHeader::iconArea() const {
    const juce::Font f = uiFont(13.5f, true);
    auto r = getLocalBounds().withTrimmedLeft(4).withTrimmedBottom(4);
    const float tw = juce::GlyphArrangement::getStringWidth(f, text_);
    return {r.getX() + tw + 6.0f, (float)r.getBottom() - 15.0f, 13.0f, 13.0f};
}

void SectionHeader::paint(juce::Graphics& g) {
    const Palette& p = palette();
    const juce::Font f = uiFont(13.5f, true);
    g.setColour(p.text);
    g.setFont(f);
    auto r = getLocalBounds().withTrimmedLeft(4).withTrimmedBottom(4);
    g.drawText(text_, r, juce::Justification::bottomLeft, true);
    if (info_.isNotEmpty()) drawIcon(g, Icon::Info, iconArea(), isMouseOver() ? p.accent : p.text3, 1.2f);
}

void SectionHeader::mouseUp(const juce::MouseEvent& e) {
    if (info_.isEmpty() || !getLocalBounds().contains(e.getPosition())) return;
    showInfoBubble(info_, localAreaToGlobal(iconArea().toNearestInt().expanded(3)));
}

namespace {
/// Wrapped explanation text inside the bubble.
class InfoText : public juce::Component {
public:
    explicit InfoText(const juce::String& text) {
        const Palette& p = palette();
        str_.setText(text);
        str_.setFont(uiFont(13.0f));
        str_.setColour(p.text);
        str_.setLineSpacing(3.0f);
        juce::TextLayout tl;
        tl.createLayout(str_, (float)kWidth);
        setSize(kWidth + 2 * kPad, juce::jmin(520, (int)std::ceil(tl.getHeight())) + 2 * kPad);
    }
    void paint(juce::Graphics& g) override {
        juce::TextLayout tl;
        tl.createLayout(str_, (float)kWidth);
        tl.draw(g, getLocalBounds().reduced(kPad).toFloat());
    }

private:
    static constexpr int kWidth = 320, kPad = 14;
    juce::AttributedString str_;
};
} // namespace

void showInfoBubble(const juce::String& text, juce::Rectangle<int> targetScreenArea) {
    juce::CallOutBox::launchAsynchronously(std::make_unique<InfoText>(text), targetScreenArea, nullptr);
}

// ---------------------------------------------------------------------------
IconButton::IconButton(Icon icon, const juce::String& tooltip) : juce::Button(tooltip), icon_(icon) {
    setTooltip(tooltip);
    setWantsKeyboardFocus(false);
}

void IconButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const Palette& p = palette();
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    if (down) g.setColour(p.select);
    else if (highlighted) g.setColour(p.hover.withMultipliedAlpha(1.6f));
    else g.setColour(juce::Colours::transparentBlack);
    g.fillRoundedRectangle(r, 6.0f);
    const float s = 16.0f;
    juce::Colour c = isEnabled() ? (highlighted ? p.text : p.text2) : p.text3.withMultipliedAlpha(0.6f);
    drawIcon(g, icon_, r.withSizeKeepingCentre(s, s), c, 1.4f);
}

// ---------------------------------------------------------------------------
ParamSlider::ParamSlider(juce::String label, double min, double max, double step, juce::String unit, int decimals,
                         double origin)
    : label_(std::move(label)), unit_(std::move(unit)), decimals_(decimals) {
    slider_.setSliderStyle(juce::Slider::LinearHorizontal);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider_.setRange(min, max, step);
    slider_.getProperties().set("origin", origin);
    slider_.setDoubleClickReturnValue(true, juce::jlimit(min, max, origin));
    slider_.setScrollWheelEnabled(true);
    slider_.onValueChange = [this] {
        updateText();
        if (onChange) onChange(slider_.getValue());
    };
    slider_.onDragEnd = [this] { if (onCommit) onCommit(); };
    addAndMakeVisible(slider_);

    value_.setJustificationType(juce::Justification::centredRight);
    value_.setEditable(false, true, false);
    value_.setColour(juce::Label::textColourId, palette().text2);
    value_.setColour(juce::Label::textWhenEditingColourId, palette().text);
    value_.setColour(juce::Label::backgroundWhenEditingColourId, palette().field);
    value_.onTextChange = [this] {
        const juce::String t = value_.getText().replaceCharacter(',', '.').retainCharacters("0123456789.-+");
        if (t.isNotEmpty()) {
            slider_.setValue(t.getDoubleValue(), juce::sendNotificationSync);
            if (onCommit) onCommit();
        }
        updateText();
    };
    addAndMakeVisible(value_);
    updateText();
}

void ParamSlider::setValue(double v, juce::NotificationType n) {
    slider_.setValue(v, n);
    updateText();
}

void ParamSlider::updateText() {
    const double v = slider_.getValue();
    value_.setText(formatter ? formatter(v) : num(v, decimals_) + (unit_.isEmpty() ? "" : " " + unit_), juce::dontSendNotification);
}

namespace {
int labelWidth(int w) { return juce::jlimit(72, 120, w * 31 / 100); }
} // namespace

namespace {
/// Room for the label: the usual column, wider for a long label so it never runs into the slider.
int sliderLabelWidth(const juce::String& label, int w) {
    const int text = juce::roundToInt(juce::GlyphArrangement::getStringWidth(uiFont(13.5f), label)) + 12;
    return juce::jmax(labelWidth(w), juce::jmin(text, w * 45 / 100));
}
} // namespace

void ParamSlider::resized() {
    auto r = getLocalBounds();
    r.removeFromLeft(sliderLabelWidth(label_, getWidth()));
    value_.setBounds(r.removeFromRight(60));
    slider_.setBounds(r);
}

void ParamSlider::paint(juce::Graphics& g) {
    g.setColour(slider_.isEnabled() ? palette().text : palette().text3);
    g.setFont(uiFont(13.5f));
    g.drawText(label_, getLocalBounds().withWidth(sliderLabelWidth(label_, getWidth())), juce::Justification::centredLeft, true);
}

// ---------------------------------------------------------------------------
void ReductionMeter::setReductionDb(float db) {
    db = std::isfinite(db) ? juce::jlimit(0.0f, 60.0f, db) : 0.0f;
    const float before = shown_, holdBefore = hold_;
    shown_ = db > shown_ ? db : shown_ + (db - shown_) * 0.25f; // instant up, smooth down
    if (shown_ < 0.01f) shown_ = 0.0f;
    if (db >= hold_) { hold_ = db; holdTicks_ = 45; }
    else if (--holdTicks_ < 0) hold_ = juce::jmax(shown_, hold_ - 0.3f);
    if (std::abs(before - shown_) > 0.005f || std::abs(holdBefore - hold_) > 0.005f) repaint();
}

void ReductionMeter::paint(juce::Graphics& g) {
    const Palette& p = palette();
    const int lw = sliderLabelWidth(label_, getWidth());
    g.setColour(p.text);
    g.setFont(uiFont(13.5f));
    g.drawText(label_, getLocalBounds().withWidth(lw), juce::Justification::centredLeft, true);
    auto r = getLocalBounds().withTrimmedLeft(lw);
    const auto valueArea = r.removeFromRight(60);
    const auto bar = r.toFloat().reduced(6.0f, 0.0f).withSizeKeepingCentre(r.getWidth() - 12.0f, 6.0f);
    g.setColour(p.track);
    g.fillRoundedRectangle(bar, 3.0f);
    auto unit = [](float db) { return juce::jlimit(0.0f, 1.0f, db / kRangeDb); };
    if (shown_ > 0.0f) {
        const float w = bar.getWidth() * unit(shown_);
        g.setColour(p.warn);
        g.fillRoundedRectangle(bar.withLeft(bar.getRight() - juce::jmax(3.0f, w)), 3.0f);
    }
    if (hold_ > 0.05f) {
        const float x = bar.getRight() - bar.getWidth() * unit(hold_);
        g.setColour(p.warn.withAlpha(0.8f));
        g.fillRect(x - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
    }
    g.setColour(shown_ > 0.05f ? p.text : p.text3);
    g.setFont(uiFont(12.5f));
    g.drawText(shown_ > 0.05f ? juce::String::fromUTF8("\xe2\x88\x92") + num(hold_, 1) + " dB" : juce::String("0 dB"), valueArea,
               juce::Justification::centredRight, false);
}

// ---------------------------------------------------------------------------
ChoiceRow::ChoiceRow(juce::String label, const juce::StringArray& items) : label_(std::move(label)) {
    box_.addItemList(items, 1);
    box_.onChange = [this] {
        if (onChange && box_.getSelectedItemIndex() >= 0) onChange(box_.getSelectedItemIndex());
    };
    addAndMakeVisible(box_);
}

void ChoiceRow::resized() {
    auto r = getLocalBounds().withSizeKeepingCentre(getWidth(), juce::jmin(getHeight(), 26));
    if (label_.isNotEmpty()) r.removeFromLeft(labelWidth(getWidth()));
    box_.setBounds(r);
}

void ChoiceRow::paint(juce::Graphics& g) {
    if (label_.isEmpty()) return;
    g.setColour(palette().text);
    g.setFont(uiFont(13.5f));
    g.drawText(label_, getLocalBounds().withWidth(sliderLabelWidth(label_, getWidth())), juce::Justification::centredLeft, true);
}

// ---------------------------------------------------------------------------
TextRow::TextRow(juce::String label) : label_(std::move(label)) {
    editor_.setFont(uiFont(13.5f));
    editor_.setIndents(8, 5);
    editor_.onReturnKey = [this] { if (onCommit) onCommit(editor_.getText()); };
    editor_.onFocusLost = [this] { if (onCommit) onCommit(editor_.getText()); };
    addAndMakeVisible(editor_);
}

void TextRow::resized() {
    auto r = getLocalBounds().withSizeKeepingCentre(getWidth(), juce::jmin(getHeight(), 26));
    r.removeFromLeft(labelWidth(getWidth()));
    editor_.setBounds(r);
}

void TextRow::paint(juce::Graphics& g) {
    g.setColour(palette().text);
    g.setFont(uiFont(13.5f));
    g.drawText(label_, getLocalBounds().withWidth(sliderLabelWidth(label_, getWidth())), juce::Justification::centredLeft, true);
}

// ---------------------------------------------------------------------------
Segmented::Segmented(const juce::StringArray& items, bool numbered) : items_(items), numbered_(numbered) {}

void Segmented::setSelected(int index) {
    selected_ = juce::jlimit(0, juce::jmax(0, items_.size() - 1), index);
    repaint();
}

std::vector<juce::Rectangle<int>> Segmented::segmentBounds() const {
    std::vector<juce::Rectangle<int>> out;
    auto r = getLocalBounds().reduced(3);
    if (items_.isEmpty()) return out;
    // Width proportional to the text so the steps look like Projection Studio's.
    std::vector<float> widths;
    float total = 0.0f;
    const juce::Font f = uiFont(13.5f, true);
    for (const auto& s : items_) {
        const float w = juce::GlyphArrangement::getStringWidth(f, s) + (numbered_ ? 54.0f : 28.0f);
        widths.push_back(w);
        total += w;
    }
    float x = (float)r.getX();
    const float scale = (float)r.getWidth() / total;
    for (float w : widths) {
        const int x0 = juce::roundToInt(x);
        x += w * scale;
        out.emplace_back(x0, r.getY(), juce::roundToInt(x) - x0, r.getHeight());
    }
    return out;
}

int Segmented::idealWidth() const {
    float total = 6.0f;
    const juce::Font f = uiFont(13.5f, true);
    for (const auto& s : items_) total += juce::GlyphArrangement::getStringWidth(f, s) + (numbered_ ? 54.0f : 28.0f);
    return juce::roundToInt(total);
}

int Segmented::indexAt(juce::Point<int> p) const {
    const auto b = segmentBounds();
    for (size_t i = 0; i < b.size(); ++i)
        if (b[i].contains(p)) return static_cast<int>(i);
    return -1;
}

OptionCards::OptionCards(std::vector<Option> options) : options_(std::move(options)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void OptionCards::setSelected(int index) {
    selected_ = juce::jlimit(0, juce::jmax(0, (int)options_.size() - 1), index);
    repaint();
}

void OptionCards::paint(juce::Graphics& g) {
    const Palette& p = palette();
    for (int i = 0; i < (int)options_.size(); ++i) {
        auto r = juce::Rectangle<float>(0.0f, (float)(i * kRowHeight), (float)getWidth(), (float)kRowHeight).reduced(0.0f, 2.0f);
        const bool sel = i == selected_;
        g.setColour(sel ? p.accent.withAlpha(0.16f) : (i == hover_ ? p.hover : p.field));
        g.fillRoundedRectangle(r, 8.0f);
        if (sel) {
            g.setColour(p.accent);
            g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.2f);
        }
        // Radio mark.
        auto dot = juce::Rectangle<float>(r.getX() + 12.0f, r.getCentreY() - 8.0f, 16.0f, 16.0f);
        g.setColour(sel ? p.accent : p.lineStrong);
        g.drawEllipse(dot.reduced(0.75f), 1.5f);
        if (sel) g.fillEllipse(dot.reduced(4.5f));
        auto text = r.withTrimmedLeft(40.0f).withTrimmedRight(10.0f);
        g.setColour(p.text);
        g.setFont(uiFont(13.5f, true));
        g.drawText(options_[(size_t)i].title, text.removeFromTop(r.getHeight() * 0.5f).withTrimmedTop(3.0f), juce::Justification::bottomLeft, true);
        g.setColour(p.text2);
        g.setFont(uiFont(12.0f));
        g.drawText(options_[(size_t)i].text, text.withTrimmedBottom(3.0f), juce::Justification::topLeft, true);
    }
}

void OptionCards::mouseUp(const juce::MouseEvent& e) {
    const int i = e.y / kRowHeight;
    if (i < 0 || i >= (int)options_.size() || i == selected_) return;
    selected_ = i;
    repaint();
    if (onChange) onChange(i);
}

void OptionCards::mouseMove(const juce::MouseEvent& e) {
    const int i = juce::jlimit(-1, (int)options_.size() - 1, e.y / kRowHeight);
    if (i != hover_) { hover_ = i; repaint(); }
}

void OptionCards::mouseExit(const juce::MouseEvent&) {
    hover_ = -1;
    repaint();
}

ActionList::ActionList(std::vector<Action> actions) : actions_(std::move(actions)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ActionList::paint(juce::Graphics& g) {
    const Palette& p = palette();
    auto all = getLocalBounds().toFloat();
    g.setColour(p.group);
    g.fillRoundedRectangle(all, 10.0f);
    g.setColour(p.line);
    g.drawRoundedRectangle(all.reduced(0.25f), 10.0f, 0.75f);
    for (int i = 0; i < (int)actions_.size(); ++i) {
        auto r = juce::Rectangle<float>(0.0f, (float)(i * kRowHeight), (float)getWidth(), (float)kRowHeight);
        if (i == hover_) {
            g.setColour(p.hover);
            g.fillRoundedRectangle(r.reduced(2.0f), 8.0f);
        }
        if (i > 0) {
            g.setColour(p.line);
            g.drawHorizontalLine((int)r.getY(), r.getX() + 12.0f, r.getRight() - 12.0f);
        }
        auto text = r.reduced(12.0f, 0.0f).withTrimmedRight(18.0f);
        g.setColour(i == 0 ? p.accent : p.text);
        g.setFont(uiFont(13.5f, true));
        g.drawText(actions_[(size_t)i].title, text.removeFromTop(r.getHeight() * 0.5f).withTrimmedTop(3.0f), juce::Justification::bottomLeft, true);
        g.setColour(p.text2);
        g.setFont(uiFont(12.0f));
        g.drawText(actions_[(size_t)i].text, text.withTrimmedBottom(3.0f), juce::Justification::topLeft, true);
        // Chevron.
        juce::Path c;
        const float cx = r.getRight() - 18.0f, cy = r.getCentreY();
        c.startNewSubPath(cx - 3.0f, cy - 5.0f);
        c.lineTo(cx + 2.0f, cy);
        c.lineTo(cx - 3.0f, cy + 5.0f);
        g.setColour(p.text3);
        g.strokePath(c, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void ActionList::mouseUp(const juce::MouseEvent& e) {
    const int i = e.y / kRowHeight;
    if (i >= 0 && i < (int)actions_.size() && getLocalBounds().contains(e.getPosition()) && actions_[(size_t)i].run)
        actions_[(size_t)i].run();
}

void ActionList::mouseMove(const juce::MouseEvent& e) {
    const int i = juce::jlimit(-1, (int)actions_.size() - 1, e.y / kRowHeight);
    if (i != hover_) { hover_ = i; repaint(); }
}

void ActionList::mouseExit(const juce::MouseEvent&) {
    hover_ = -1;
    repaint();
}

void Segmented::paint(juce::Graphics& g) {
    const Palette& p = palette();
    g.setColour(p.field);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    const auto b = segmentBounds();
    for (size_t i = 0; i < b.size(); ++i) {
        const bool sel = static_cast<int>(i) == selected_;
        auto r = b[i].toFloat();
        if (sel) {
            g.setColour(isDark() ? p.select.withMultipliedAlpha(1.8f) : juce::Colours::white);
            g.fillRoundedRectangle(r, 6.0f);
        } else if (static_cast<int>(i) == hover_) {
            g.setColour(p.hover);
            g.fillRoundedRectangle(r, 6.0f);
        }
        auto inner = b[i];
        if (numbered_) {
            const juce::String label = items_[static_cast<int>(i)];
            const float tw = juce::GlyphArrangement::getStringWidth(uiFont(13.5f, true), label);
            const int total = 18 + 8 + juce::roundToInt(tw) + 6;
            auto row = inner.withSizeKeepingCentre(total, inner.getHeight());
            auto badge = row.removeFromLeft(18).withSizeKeepingCentre(18, 18).toFloat();
            g.setColour(sel ? p.accent : p.lineStrong);
            g.fillEllipse(badge);
            g.setColour(sel ? p.onAccent : p.text2);
            g.setFont(uiFont(11.0f, true));
            g.drawText(juce::String((int)i + 1), badge, juce::Justification::centred, false);
            row.removeFromLeft(8);
            g.setColour(sel ? p.text : p.text2);
            g.setFont(uiFont(13.5f, sel));
            g.drawText(label, row, juce::Justification::centredLeft, false);
        } else {
            g.setColour(sel ? p.text : p.text2);
            g.setFont(uiFont(13.0f, sel));
            g.drawText(items_[static_cast<int>(i)], inner, juce::Justification::centred, true);
        }
    }
}

void Segmented::mouseUp(const juce::MouseEvent& e) {
    const int i = indexAt(e.getPosition());
    if (i >= 0 && i != selected_) {
        selected_ = i;
        repaint();
        if (onChange) onChange(i);
    }
}

void Segmented::mouseMove(const juce::MouseEvent& e) {
    const int i = indexAt(e.getPosition());
    if (i != hover_) { hover_ = i; repaint(); }
}

void Segmented::mouseExit(const juce::MouseEvent&) { hover_ = -1; repaint(); }

// ---------------------------------------------------------------------------
void StatusCard::set(Kind k, juce::String title, juce::String detail) {
    kind_ = k;
    title_ = std::move(title);
    detail_ = std::move(detail);
    repaint();
}

int StatusCard::idealHeight(int width) const {
    juce::AttributedString a;
    a.append(detail_, uiFont(12.5f));
    juce::TextLayout t;
    t.createLayout(a, (float)juce::jmax(50, width - 64));
    return juce::jmax(64, 34 + juce::roundToInt(t.getHeight()) + 14 + (total_ > 0 ? 12 : 0));
}

void StatusCard::paint(juce::Graphics& g) {
    const Palette& p = palette();
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(p.group);
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(p.line);
    g.drawRoundedRectangle(r, 10.0f, 1.0f);
    const juce::Colour c = kind_ == Kind::Ok ? p.ok : (kind_ == Kind::Warn ? p.warn : p.accent);
    auto icon = juce::Rectangle<float>(14, 14, 26, 26);
    g.setColour(c.withAlpha(0.22f));
    g.fillEllipse(icon);
    g.setColour(c);
    if (kind_ == Kind::Ok) {
        juce::Path t;
        t.startNewSubPath(icon.getX() + 7.5f, icon.getCentreY());
        t.lineTo(icon.getX() + 11.5f, icon.getY() + 17.0f);
        t.lineTo(icon.getX() + 19.0f, icon.getY() + 9.0f);
        g.strokePath(t, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    } else {
        g.setFont(uiFont(15.0f, true));
        g.drawText(kind_ == Kind::Warn ? "!" : "i", icon, juce::Justification::centred, false);
    }
    g.setColour(p.text);
    g.setFont(uiFont(14.0f, true));
    g.drawText(title_, juce::Rectangle<float>(52, 12, r.getWidth() - 62, 18), juce::Justification::centredLeft, true);
    juce::AttributedString a;
    a.append(detail_, uiFont(12.5f), p.text2);
    juce::TextLayout t;
    t.createLayout(a, r.getWidth() - 64);
    t.draw(g, juce::Rectangle<float>(52, 32, r.getWidth() - 64, r.getHeight() - 36));
    if (total_ > 0) {
        const int n = juce::jmin(total_, 12);
        const float gap = 4.0f, x0 = 14.0f, w = r.getWidth() - 28.0f;
        const float seg = (w - gap * (n - 1)) / n;
        const int filledSegs = juce::roundToInt((float)filled_ / (float)total_ * n);
        for (int i = 0; i < n; ++i) {
            g.setColour(i < filledSegs ? c : p.select);
            g.fillRoundedRectangle(x0 + i * (seg + gap), r.getBottom() - 12.0f, seg, 3.0f, 1.5f);
        }
    }
}

// ---------------------------------------------------------------------------
void SidebarList::setRows(std::vector<Row> rows) {
    rows_ = std::move(rows);
    if (selected_ >= (int)rows_.size()) selected_ = (int)rows_.size() - 1;
    setSize(getWidth(), contentHeight());
    repaint();
}

void SidebarList::setSelected(int row) {
    selected_ = row;
    repaint();
}

void SidebarList::setLevels(const std::vector<float>& levels) {
    bool changed = false;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const float v = i < levels.size() ? levels[i] : 0.0f; // rows without a level show no bar
        if (std::abs(rows_[i].level - v) > 0.01f) { rows_[i].level = v; changed = true; }
    }
    if (changed) repaint();
}

std::vector<SidebarList::Item> SidebarList::layoutItems() const {
    std::vector<Item> out;
    int y = 8;
    juce::String group = "\x01";
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].group != group) {
            group = rows_[i].group;
            if (group.isNotEmpty()) y += (i == 0 ? 6 : 14) + 18;
        }
        out.push_back({(int)i, juce::Rectangle<int>(8, y, juce::jmax(10, getWidth() - 16), 42)});
        y += 43;
    }
    return out;
}

int SidebarList::contentHeight() const {
    const auto items = layoutItems();
    return items.empty() ? 40 : items.back().bounds.getBottom() + 12;
}

void SidebarList::paint(juce::Graphics& g) {
    const Palette& p = palette();
    const auto items = layoutItems();
    juce::String group = "\x01";
    for (const auto& it : items) {
        const Row& row = rows_[(size_t)it.row];
        if (row.group != group) {
            group = row.group;
            if (group.isNotEmpty()) {
                g.setColour(p.text3);
                g.setFont(uiFont(12.0f, true));
                g.drawText(group, it.bounds.getX() + 4, it.bounds.getY() - 22, it.bounds.getWidth(), 18, juce::Justification::bottomLeft);
            }
        }
        auto r = it.bounds.toFloat();
        if (it.row == selected_) {
            g.setColour(p.select);
            g.fillRoundedRectangle(r, 8.0f);
        } else if (it.row == hover_) {
            g.setColour(p.hover);
            g.fillRoundedRectangle(r, 8.0f);
        }
        auto badge = juce::Rectangle<float>(r.getX() + 10, r.getCentreY() - 10, 20, 20);
        drawNumberBadge(g, badge, row.number, row.dim ? row.colour.withSaturation(0.0f).withAlpha(0.6f) : row.colour);
        const float rightPad = row.toggles ? (row.trailing.isNotEmpty() ? 74.0f : 58.0f) : (row.trailing.isNotEmpty() || row.check || row.menu ? 64.0f : 8.0f);
        auto text = r.withTrimmedLeft(42).withTrimmedRight(rightPad);
        g.setColour(row.dim ? p.text3 : p.text);
        g.setFont(uiFont(13.5f));
        g.drawText(row.title, text.withHeight(r.getHeight() * 0.55f).translated(0, 3), juce::Justification::centredLeft, true);
        g.setColour(p.text3);
        g.setFont(uiFont(11.5f));
        g.drawText(row.subtitle, text.withTrimmedTop(r.getHeight() * 0.5f).withTrimmedBottom(3), juce::Justification::centredLeft, true);
        if (row.toggles) {
            for (int w = 0; w < 2; ++w) {
                const bool on = w == 0 ? row.mute : row.solo;
                const auto b = toggleRect(r, w);
                const juce::Colour c = w == 0 ? p.warn : p.accent;
                g.setColour(on ? c : p.field);
                g.fillRoundedRectangle(b, 4.0f);
                if (!on) {
                    g.setColour(p.lineStrong);
                    g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
                }
                g.setColour(on ? p.onAccent : p.text2);
                g.setFont(uiFont(11.0f, true));
                g.drawText(w == 0 ? "M" : "S", b, juce::Justification::centred, false);
            }
            if (row.trailing.isNotEmpty()) {
                g.setColour(p.text2);
                g.setFont(uiFont(12.0f));
                g.drawText(row.trailing, juce::Rectangle<float>(toggleRect(r, 0).getX() - 18, r.getY(), 16, r.getHeight()), juce::Justification::centred, false);
            }
        } else if (row.trailing.isNotEmpty()) {
            g.setColour(p.text2);
            g.setFont(uiFont(12.0f));
            g.drawText(row.trailing, r.withTrimmedRight(10).withLeft(r.getRight() - 70), juce::Justification::centredRight, true);
        } else if (row.menu && (it.row == hover_ || it.row == selected_)) {
            // "…": edit, duplicate, remove.
            const auto m = menuRect(r);
            g.setColour(p.field);
            g.fillRoundedRectangle(m, 5.0f);
            g.setColour(p.text2);
            for (int k = -1; k <= 1; ++k) g.fillEllipse(m.getCentreX() + (float)k * 5.0f - 1.5f, m.getCentreY() - 1.5f, 3.0f, 3.0f);
        } else if (row.check) {
            auto c = juce::Rectangle<float>(r.getRight() - 28, r.getCentreY() - 8, 16, 16);
            g.setColour(p.ok);
            g.drawEllipse(c, 1.4f);
            juce::Path t;
            t.startNewSubPath(c.getX() + 4.5f, c.getCentreY());
            t.lineTo(c.getX() + 7.0f, c.getY() + 11.0f);
            t.lineTo(c.getX() + 11.5f, c.getY() + 5.5f);
            g.strokePath(t, juce::PathStrokeType(1.4f));
        }
        if (row.level > 0.001f) {
            const float w = (r.getWidth() - 52) * juce::jlimit(0.0f, 1.0f, row.level);
            g.setColour(row.level > 0.95f ? p.bad : p.ok.withAlpha(0.8f));
            g.fillRoundedRectangle(r.getX() + 42, r.getBottom() - 4, w, 2.0f, 1.0f);
        }
    }
}

juce::Rectangle<float> SidebarList::menuRect(juce::Rectangle<float> row) {
    return {row.getRight() - 34.0f, row.getCentreY() - 11.0f, 26.0f, 22.0f};
}

juce::Rectangle<float> SidebarList::toggleRect(juce::Rectangle<float> row, int which) {
    return {row.getRight() - 52.0f + (float)which * 24.0f, row.getCentreY() - 10.0f, 20.0f, 20.0f};
}

void SidebarList::mouseUp(const juce::MouseEvent& e) {
    for (const auto& it : layoutItems())
        if (it.bounds.contains(e.getPosition())) {
            const Row& row = rows_[(size_t)it.row];
            if (row.menu && onMenu && (e.mods.isPopupMenu() || menuRect(it.bounds.toFloat()).contains(e.position))) {
                selected_ = it.row;
                repaint();
                if (onSelect) onSelect(it.row);
                onMenu(it.row, localAreaToGlobal(menuRect(it.bounds.toFloat()).toNearestInt()));
                return;
            }
            if (row.toggles)
                for (int w = 0; w < 2; ++w)
                    if (toggleRect(it.bounds.toFloat(), w).expanded(2.0f).contains(e.position)) {
                        if (onToggle) onToggle(it.row, w);
                        return;
                    }
            selected_ = it.row;
            repaint();
            if (onSelect) onSelect(it.row);
            return;
        }
}

void SidebarList::mouseMove(const juce::MouseEvent& e) {
    int h = -1;
    for (const auto& it : layoutItems())
        if (it.bounds.contains(e.getPosition())) h = it.row;
    if (h != hover_) { hover_ = h; repaint(); }
}

void SidebarList::mouseExit(const juce::MouseEvent&) {
    if (hover_ != -1) { hover_ = -1; repaint(); }
}

// ---------------------------------------------------------------------------
OutputStrip::OutputStrip() { setRepaintsOnMouseActivity(true); }

void OutputStrip::setGainDb(float db) {
    if (dragging_ || std::abs(db - gainDb_) < 1e-4f) return;
    gainDb_ = db;
    repaint();
}

void OutputStrip::setLevel(float unit) {
    unit = juce::jlimit(0.0f, 1.0f, unit);
    if (std::abs(unit - level_) < 0.01f) return;
    level_ = unit;
    repaint();
}

void OutputStrip::setReductionDb(float db) {
    db = std::isfinite(db) ? juce::jlimit(0.0f, 60.0f, db) : 0.0f;
    const float before = reduction_;
    if (db >= reduction_) { reduction_ = db; reductionHold_ = 30; } // hold about a second
    else if (--reductionHold_ < 0) reduction_ = db < 0.05f ? juce::jmax(0.0f, reduction_ - 0.5f) : db;
    if (reduction_ < 0.05f) reduction_ = 0.0f;
    if (std::abs(before - reduction_) > 0.01f) repaint();
}

juce::Rectangle<float> OutputStrip::track() const {
    return getLocalBounds().toFloat().reduced(16.0f, 0.0f).withTop(30.0f).withHeight(16.0f);
}

float OutputStrip::unitForDb(float db) const { return juce::jmap(juce::jlimit(kMinDb, kMaxDb, db), kMinDb, kMaxDb, 0.0f, 1.0f); }

float OutputStrip::dbForX(float x) const {
    const auto t = track();
    const float db = juce::jmap(juce::jlimit(t.getX(), t.getRight(), x), t.getX(), t.getRight(), kMinDb, kMaxDb);
    return std::round(db * 2.0f) / 2.0f; // half-dB steps
}

void OutputStrip::paint(juce::Graphics& g) {
    const Palette& p = palette();
    g.setColour(p.line);
    g.fillRect(0.0f, 0.0f, (float)getWidth(), 1.0f);
    g.setColour(p.text);
    g.setFont(uiFont(13.0f, true));
    g.drawText(tr("Master"), 16, 8, getWidth() / 2, 18, juce::Justification::centredLeft, false);
    if (reduction_ > 0.0f) { // the limiter works right now
        const int mw = juce::roundToInt(juce::GlyphArrangement::getStringWidth(uiFont(13.0f, true), tr("Master")));
        g.setColour(p.warn);
        g.setFont(uiFont(11.5f, true));
        g.drawText(tr("Limiter") + " " + juce::String::fromUTF8("\xe2\x88\x92") + num(reduction_, 1) + " dB", 16 + mw + 8, 8, getWidth() / 2, 18,
                   juce::Justification::centredLeft, false);
    }
    g.setColour(p.text2);
    g.setFont(uiFont(12.0f));
    const juce::String v = gainDb_ <= kMinDb + 0.01f ? juce::String::fromUTF8("\xe2\x88\x92\xe2\x88\x9e") : num(gainDb_, 1) + " dB";
    g.drawText(v, getWidth() / 2, 8, getWidth() / 2 - 16, 18, juce::Justification::centredRight, false);

    const auto t = track();
    const float cy = t.getCentreY();
    // Fader track with the 0 dB mark.
    g.setColour(p.track);
    g.fillRoundedRectangle(t.getX(), cy - 2.0f, t.getWidth(), 4.0f, 2.0f);
    const float x = t.getX() + t.getWidth() * unitForDb(gainDb_);
    g.setColour(p.accent);
    g.fillRoundedRectangle(t.getX(), cy - 2.0f, x - t.getX(), 4.0f, 2.0f);
    const float zx = t.getX() + t.getWidth() * unitForDb(0.0f);
    g.setColour(p.text3);
    g.fillRect(zx - 0.5f, cy + 4.0f, 1.0f, 4.0f);
    g.setColour(p.knob);
    g.fillEllipse(x - 7.0f, cy - 7.0f, 14.0f, 14.0f);
    g.setColour(p.lineStrong);
    g.drawEllipse(x - 7.0f, cy - 7.0f, 14.0f, 14.0f, 0.75f);
    // Output meter: the loudest output channel.
    const auto m = juce::Rectangle<float>(t.getX(), t.getBottom() + 6.0f, t.getWidth(), 4.0f);
    g.setColour(p.select);
    g.fillRoundedRectangle(m, 2.0f);
    if (level_ > 0.001f) {
        g.setColour(level_ > 0.95f ? p.bad : (level_ > 0.8f ? p.warn : p.ok));
        g.fillRoundedRectangle(m.withWidth(m.getWidth() * level_), 2.0f);
    }
}

void OutputStrip::mouseDown(const juce::MouseEvent& e) {
    if (!track().expanded(0.0f, 8.0f).contains(e.position)) return;
    dragging_ = true;
    gainDb_ = dbForX(e.position.x);
    if (onChange) onChange(gainDb_);
    repaint();
}

void OutputStrip::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_) return;
    const float db = dbForX(e.position.x);
    if (std::abs(db - gainDb_) < 1e-4f) return;
    gainDb_ = db;
    if (onChange) onChange(gainDb_);
    repaint();
}

void OutputStrip::mouseUp(const juce::MouseEvent&) {
    if (!dragging_) return;
    dragging_ = false;
    if (onCommit) onCommit();
}

void OutputStrip::mouseDoubleClick(const juce::MouseEvent&) {
    dragging_ = false;
    gainDb_ = 0.0f;
    if (onChange) onChange(0.0f);
    if (onCommit) onCommit();
    repaint();
}

// ---------------------------------------------------------------------------
FormPage::FormPage() {
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(8);
    addAndMakeVisible(viewport_);
}

void FormPage::clear() {
    for (auto& e : entries_) content_.removeChildComponent(e.comp.get());
    entries_.clear();
    relayout();
}

void FormPage::addRow(std::unique_ptr<juce::Component> c, int height) {
    content_.addAndMakeVisible(*c);
    entries_.push_back({std::move(c), height});
    relayout();
}

void FormPage::addSpace(int height) { addRow(std::make_unique<juce::Component>(), height); }

void FormPage::addDetails(const juce::String& text) {
    for (auto& e : entries_)
        if (auto* h = dynamic_cast<SectionHeader*>(e.comp.get())) {
            h->appendInfo(text);
            return;
        }
}

void FormPage::setRowHeight(juce::Component* c, int height) {
    for (auto& e : entries_)
        if (e.comp.get() == c) e.height = height;
    relayout();
}

namespace {
bool isSettingRow(juce::Component* c) {
    return dynamic_cast<ParamSlider*>(c) != nullptr || dynamic_cast<ChoiceRow*>(c) != nullptr ||
           dynamic_cast<TextRow*>(c) != nullptr || dynamic_cast<juce::ToggleButton*>(c) != nullptr ||
           dynamic_cast<ReductionMeter*>(c) != nullptr;
}
} // namespace

void FormPage::Content::paint(juce::Graphics& g) {
    const Palette& p = palette();
    for (const auto& c : cards) {
        g.setColour(p.group);
        g.fillRoundedRectangle(c, 10.0f);
        g.setColour(p.line);
        g.drawRoundedRectangle(c.reduced(0.25f), 10.0f, 0.75f);
    }
    g.setColour(p.line);
    for (const auto& l : separators) g.drawLine(l, 0.75f);
}

void FormPage::relayout() {
    const int w = juce::jmax(100, viewport_.getMaximumVisibleWidth() > 0 ? viewport_.getMaximumVisibleWidth() : getWidth());
    const int pad = 12, inner = 12;
    content_.cards.clear();
    content_.separators.clear();
    int y = 8;
    int groupTop = -1;
    buttonRowY_ = -1;
    buttonRowRight_ = 0;
    auto closeGroup = [&] {
        if (groupTop < 0) return;
        content_.cards.emplace_back((float)pad, (float)groupTop, (float)(w - 2 * pad), (float)(y - groupTop));
        groupTop = -1;
        y += 10;
    };
    for (auto& e : entries_) {
        juce::Component* c = e.comp.get();
        if (isSettingRow(c)) {
            if (buttonRowY_ >= 0) { y = buttonRowY_ + 36; buttonRowY_ = -1; }
            const int h = juce::jmax(e.height, 38);
            if (groupTop < 0) groupTop = y;
            else content_.separators.emplace_back((float)(pad + inner), (float)y, (float)(w - pad), (float)y);
            c->setBounds(pad + inner, y, w - 2 * (pad + inner), h);
            y += h;
            continue;
        }
        closeGroup();
        if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && !(bool)b->getProperties().getWithDefault("block", false)) {
            // Action buttons: right-aligned, consecutive ones share a row when they fit.
            const int bw = juce::jmin(w - 2 * pad, juce::roundToInt(juce::GlyphArrangement::getStringWidth(uiFont(13.5f), b->getButtonText())) + 30);
            if (buttonRowRight_ - bw < pad || buttonRowY_ < 0) {
                if (buttonRowY_ >= 0) y += 34;
                buttonRowY_ = y;
                buttonRowRight_ = w - pad;
            }
            b->setBounds(buttonRowRight_ - bw, buttonRowY_, bw, 28);
            buttonRowRight_ -= bw + 8;
            continue;
        }
        if (buttonRowY_ >= 0) { y = buttonRowY_ + 36; buttonRowY_ = -1; }
        if (auto* n = dynamic_cast<NoteRow*>(c)) e.height = n->heightFor(w - 2 * pad);
        c->setBounds(pad, y, w - 2 * pad, e.height);
        y += e.height + (dynamic_cast<SectionHeader*>(c) != nullptr ? 2 : 8);
    }
    closeGroup();
    if (buttonRowY_ >= 0) { y = buttonRowY_ + 36; buttonRowY_ = -1; }
    content_.setSize(w, y + 16);
    content_.repaint();
}

void FormPage::resized() {
    viewport_.setBounds(getLocalBounds());
    relayout();
}

void BackButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const Palette& p = palette();
    const auto f = uiFont(13.5f);
    const float tw = juce::GlyphArrangement::getStringWidth(f, getButtonText());
    auto r = getLocalBounds().toFloat().withWidth(juce::jmin((float)getWidth(), tw + 34.0f)).reduced(0.0f, 3.0f);
    if (highlighted || down) {
        g.setColour(down ? p.select : p.hover);
        g.fillRoundedRectangle(r, 6.0f);
    }
    const juce::Colour c = p.accent.withMultipliedAlpha(down ? 0.8f : 1.0f);
    juce::Path chevron;
    const float cx = r.getX() + 12.0f, cy = r.getCentreY();
    chevron.startNewSubPath(cx + 3.0f, cy - 6.0f);
    chevron.lineTo(cx - 3.0f, cy);
    chevron.lineTo(cx + 3.0f, cy + 6.0f);
    g.setColour(c);
    g.strokePath(chevron, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setFont(f);
    g.drawText(getButtonText(), r.withTrimmedLeft(24.0f), juce::Justification::centredLeft, false);
}

std::unique_ptr<juce::TextButton> makeButton(const juce::String& text, bool primary, bool flat) {
    auto b = std::make_unique<juce::TextButton>(text);
    b->getProperties().set("primary", primary);
    b->getProperties().set("flat", flat);
    return b;
}

void drawNoticePill(juce::Graphics& g, float leftLimit, float rightX, float centreY, bool warn, const juce::String& text) {
    const Palette& p = palette();
    const auto f = uiFont(12.5f);
    const float tw = std::min(rightX - leftLimit - 40.0f, juce::GlyphArrangement::getStringWidth(f, text));
    if (tw < 60.0f || text.isEmpty()) return;
    const auto r = juce::Rectangle<float>(rightX - tw - 40.0f, centreY - 14.0f, tw + 40.0f, 28.0f);
    g.setColour(warn ? p.warn.withAlpha(0.14f) : p.group);
    g.fillRoundedRectangle(r, 7.0f);
    g.setColour(warn ? p.warn.withAlpha(0.45f) : p.line);
    g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);
    const auto dot = juce::Rectangle<float>(r.getX() + 9.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f);
    g.setColour(warn ? p.warn : p.text3);
    g.fillEllipse(dot);
    g.setColour(p.side);
    g.setFont(uiFont(10.5f, true));
    g.drawText(warn ? "!" : "i", dot, juce::Justification::centred);
    g.setColour(warn ? p.text : p.text2);
    g.setFont(f);
    g.drawText(text, r.withTrimmedLeft(30.0f).withTrimmedRight(8.0f), juce::Justification::centredLeft, true);
}


ExpandRow::ExpandRow(juce::String title, bool open, std::function<void()> toggle)
    : title_(std::move(title)), open_(open), toggle_(std::move(toggle)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ExpandRow::paint(juce::Graphics& g) {
    const auto& p = palette();
    const auto c = isMouseOver() ? p.text : p.text2;
    juce::Path chev;
    const float cx = 14.0f, cy = getHeight() * 0.5f;
    if (open_) {
        chev.startNewSubPath(cx - 4, cy - 2);
        chev.lineTo(cx, cy + 2);
        chev.lineTo(cx + 4, cy - 2);
    } else {
        chev.startNewSubPath(cx - 2, cy - 4);
        chev.lineTo(cx + 2, cy);
        chev.lineTo(cx - 2, cy + 4);
    }
    g.setColour(c);
    g.strokePath(chev, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setFont(uiFont(13.0f));
    g.drawText(title_, getLocalBounds().withTrimmedLeft(26), juce::Justification::centredLeft);
}

void ExpandRow::mouseUp(const juce::MouseEvent& e) {
    if (!e.mouseWasClicked()) return;
    juce::MessageManager::callAsync([t = toggle_] { t(); });
}


int NoteRow::heightFor(int width) const {
    juce::AttributedString a;
    a.setLineSpacing(2.0f);
    a.append(text_, uiFont(12.5f));
    juce::TextLayout t;
    t.createLayout(a, (float)juce::jmax(60, width - 8));
    return juce::roundToInt(t.getHeight()) + 4;
}

void NoteRow::paint(juce::Graphics& g) {
    juce::AttributedString a;
    a.setLineSpacing(2.0f);
    a.append(text_, uiFont(12.5f), palette().text2);
    juce::TextLayout t;
    t.createLayout(a, (float)juce::jmax(60, getWidth() - 8));
    t.draw(g, getLocalBounds().toFloat().withTrimmedLeft(4).withTrimmedRight(4));
}

void drawCoachBar(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& coachBadge_, const juce::String& coachTitle_,
                  const juce::String& coachText_, bool coachOk_) {
    if (coachTitle_.isEmpty()) return;
    const Palette& p = palette();
    const juce::Font tf = uiFont(13.5f, true), sf = uiFont(12.0f);
    const float textW = std::max(juce::GlyphArrangement::getStringWidth(tf, coachTitle_), juce::GlyphArrangement::getStringWidth(sf, coachText_));
    const float w = std::min((float)area.getWidth() - 32.0f, 10.0f + 28.0f + 12.0f + textW + 18.0f);
    auto r = juce::Rectangle<float>(w, 50.0f).withCentre({area.getCentreX() * 1.0f, (float)area.getBottom() - 20.0f - 25.0f});
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillRoundedRectangle(r.translated(0, 4).expanded(2), 15.0f);
    g.setColour(juce::Colour(0xff1c1c1e).withAlpha(0.88f));
    g.fillRoundedRectangle(r, 14.0f);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRoundedRectangle(r.reduced(0.25f), 14.0f, 0.75f);
    auto badge = juce::Rectangle<float>(r.getX() + 10.0f, r.getCentreY() - 14.0f, 28.0f, 28.0f);
    g.setColour(coachOk_ ? p.ok : juce::Colour(0xff0a84ff));
    g.fillEllipse(badge);
    g.setColour(juce::Colours::white);
    if (coachOk_ && coachBadge_.isEmpty()) {
        juce::Path t;
        t.startNewSubPath(badge.getX() + 8.5f, badge.getCentreY());
        t.lineTo(badge.getX() + 12.5f, badge.getY() + 18.0f);
        t.lineTo(badge.getX() + 20.0f, badge.getY() + 10.0f);
        g.strokePath(t, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    } else {
        g.setFont(uiFont(13.5f, true));
        g.drawText(coachBadge_, badge, juce::Justification::centred, false);
    }
    auto text = r.withTrimmedLeft(50.0f).withTrimmedRight(16.0f);
    g.setColour(juce::Colour(0xfff2f2f4));
    g.setFont(tf);
    g.drawText(coachTitle_, text.withTrimmedTop(8.0f).withHeight(18.0f), juce::Justification::centredLeft, true);
    g.setColour(juce::Colour(0xffebebf5).withAlpha(0.68f));
    g.setFont(sf);
    g.drawText(coachText_, text.withTrimmedTop(26.0f).withHeight(16.0f), juce::Justification::centredLeft, true);
}

} // namespace rkui
