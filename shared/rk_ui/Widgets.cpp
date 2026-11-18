#include "Widgets.h"

#include "Strings.h"

namespace rkui {

// ---------------------------------------------------------------------------
void SectionHeader::paint(juce::Graphics& g) {
    g.setColour(palette().text);
    g.setFont(uiFont(14.0f, true));
    g.drawText(text_, getLocalBounds().withTrimmedLeft(4), juce::Justification::bottomLeft, true);
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

void ParamSlider::resized() {
    auto r = getLocalBounds();
    r.removeFromLeft(juce::jmin(118, getWidth() / 3));
    value_.setBounds(r.removeFromRight(72));
    slider_.setBounds(r);
}

void ParamSlider::paint(juce::Graphics& g) {
    g.setColour(slider_.isEnabled() ? palette().text : palette().text3);
    g.setFont(uiFont(13.5f));
    g.drawText(label_, getLocalBounds().withWidth(juce::jmin(118, getWidth() / 3)).withTrimmedLeft(4),
               juce::Justification::centredLeft, true);
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
    auto r = getLocalBounds().reduced(0, 3);
    if (label_.isNotEmpty()) r.removeFromLeft(juce::jmin(118, getWidth() / 3));
    box_.setBounds(r);
}

void ChoiceRow::paint(juce::Graphics& g) {
    if (label_.isEmpty()) return;
    g.setColour(palette().text);
    g.setFont(uiFont(13.5f));
    g.drawText(label_, getLocalBounds().withWidth(juce::jmin(118, getWidth() / 3)).withTrimmedLeft(4),
               juce::Justification::centredLeft, true);
}

// ---------------------------------------------------------------------------
TextRow::TextRow(juce::String label) : label_(std::move(label)) {
    editor_.setFont(uiFont(13.5f));
    editor_.setIndents(8, 6);
    editor_.onReturnKey = [this] { if (onCommit) onCommit(editor_.getText()); };
    editor_.onFocusLost = [this] { if (onCommit) onCommit(editor_.getText()); };
    addAndMakeVisible(editor_);
}

void TextRow::resized() {
    auto r = getLocalBounds().reduced(0, 3);
    r.removeFromLeft(juce::jmin(118, getWidth() / 3));
    editor_.setBounds(r);
}

void TextRow::paint(juce::Graphics& g) {
    g.setColour(palette().text);
    g.setFont(uiFont(13.5f));
    g.drawText(label_, getLocalBounds().withWidth(juce::jmin(118, getWidth() / 3)).withTrimmedLeft(4),
               juce::Justification::centredLeft, true);
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
    return juce::jmax(64, 34 + juce::roundToInt(t.getHeight()) + 14);
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
    for (size_t i = 0; i < rows_.size() && i < levels.size(); ++i) {
        if (std::abs(rows_[i].level - levels[i]) > 0.01f) { rows_[i].level = levels[i]; changed = true; }
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
        }
        auto badge = juce::Rectangle<float>(r.getX() + 10, r.getCentreY() - 10, 20, 20);
        drawNumberBadge(g, badge, row.number, row.dim ? row.colour.withSaturation(0.0f).withAlpha(0.6f) : row.colour);
        auto text = r.withTrimmedLeft(42).withTrimmedRight(row.trailing.isNotEmpty() || row.check ? 64 : 8);
        g.setColour(row.dim ? p.text3 : p.text);
        g.setFont(uiFont(13.5f));
        g.drawText(row.title, text.withHeight(r.getHeight() * 0.55f).translated(0, 3), juce::Justification::centredLeft, true);
        g.setColour(p.text3);
        g.setFont(uiFont(11.5f));
        g.drawText(row.subtitle, text.withTrimmedTop(r.getHeight() * 0.5f).withTrimmedBottom(3), juce::Justification::centredLeft, true);
        if (row.trailing.isNotEmpty()) {
            g.setColour(p.text2);
            g.setFont(uiFont(12.0f));
            g.drawText(row.trailing, r.withTrimmedRight(10).withLeft(r.getRight() - 70), juce::Justification::centredRight, true);
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

void SidebarList::mouseUp(const juce::MouseEvent& e) {
    for (const auto& it : layoutItems())
        if (it.bounds.contains(e.getPosition())) {
            selected_ = it.row;
            repaint();
            if (onSelect) onSelect(it.row);
            return;
        }
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

void FormPage::relayout() {
    const int w = juce::jmax(100, viewport_.getMaximumVisibleWidth() > 0 ? viewport_.getMaximumVisibleWidth() : getWidth());
    int y = 6;
    for (auto& e : entries_) {
        e.comp->setBounds(12, y, w - 24, e.height);
        y += e.height + 4;
    }
    content_.setSize(w, y + 16);
}

void FormPage::resized() {
    viewport_.setBounds(getLocalBounds());
    relayout();
}

std::unique_ptr<juce::TextButton> makeButton(const juce::String& text, bool primary, bool flat) {
    auto b = std::make_unique<juce::TextButton>(text);
    b->getProperties().set("primary", primary);
    b->getProperties().set("flat", flat);
    return b;
}

} // namespace rkui
