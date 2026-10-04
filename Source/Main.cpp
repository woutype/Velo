#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow(juce::String name) 
        : DocumentWindow(name, juce::Colour(0xff060a12), DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(), true);
        setResizable(false, false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class VeloApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Velo"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }

    void initialise(const juce::String&) override {
        lookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff060a12));
        lookAndFeel.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0b1120));
        lookAndFeel.setColour(juce::ComboBox::textColourId, juce::Colour(0xfff8fafc));
        lookAndFeel.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff1e293b));
        lookAndFeel.setColour(juce::ComboBox::focusedOutlineColourId, juce::Colour(0xff0284c7));
        lookAndFeel.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff0b1120));
        lookAndFeel.setColour(juce::PopupMenu::textColourId, juce::Colour(0xfff8fafc));
        lookAndFeel.setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff0284c7));
        lookAndFeel.setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff334155));
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override {
        mainWindow.reset();
    }

private:
    juce::LookAndFeel_V4 lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(VeloApp)