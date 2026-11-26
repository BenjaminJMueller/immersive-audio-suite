#include "MainComponent.h"

namespace rkapp {

class MainWindow : public juce::DocumentWindow {
public:
    explicit MainWindow(const juce::String& name)
        : DocumentWindow(name, rkui::palette().bg, DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(), true);
        setResizable(true, true);
        setResizeLimits(980, 640, 10000, 10000);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    // After sleep or a display change macOS can drop what the window showed; only the parts
    // that animate would be drawn again. Coming back to the front redraws everything.
    void activeWindowStatusChanged() override {
        DocumentWindow::activeWindowStatusChanged();
        repaint();
    }
};

class RaumklangApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override { window_ = std::make_unique<MainWindow>("Raumklang"); }

    void shutdown() override { window_.reset(); }
    void resumed() override {
        if (window_ != nullptr) window_->repaint();
    }

    void systemRequestedQuit() override {
        if (window_ != nullptr)
            if (auto* mc = dynamic_cast<MainComponent*>(window_->getContentComponent())) mc->saveNow();
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {
        if (window_ != nullptr) window_->toFront(true);
    }

private:
    std::unique_ptr<MainWindow> window_;
};

} // namespace rkapp

START_JUCE_APPLICATION(rkapp::RaumklangApplication)
