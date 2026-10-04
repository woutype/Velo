#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class PluginSlotView : public juce::Component {
public:
    PluginSlotView(int slotIndex, 
                   std::function<void(int)> onSelect, 
                   std::function<void(int)> onOpenUI,
                   std::function<void(int, bool)> onToggleBypass,
                   std::function<void(int)> onRemove)
        : index(slotIndex), selectCb(onSelect), openUiCb(onOpenUI), 
          bypassCb(onToggleBypass), removeCb(onRemove) {
        
        updateSlotIndex(index);
        slotLabel.setFont(juce::FontOptions(12.5f, juce::Font::bold));
        slotLabel.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
        addAndMakeVisible(slotLabel);

        nameBtn.setButtonText("+ Click to Insert VST3");
        nameBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff111827));
        nameBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff64748b));
        nameBtn.onClick = [this] { selectCb(index); };
        addAndMakeVisible(nameBtn);

        bypassBtn.setButtonText("ON");
        bypassBtn.setClickingTogglesState(true);
        bypassBtn.setToggleState(true, juce::dontSendNotification);
        bypassBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff059669));
        bypassBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff059669));
        bypassBtn.setEnabled(false);
        bypassBtn.onClick = [this] {
            bool isActive = bypassBtn.getToggleState();
            bypassBtn.setButtonText(isActive ? "ON" : "BYP");
            auto col = isActive ? juce::Colour(0xff059669) : juce::Colour(0xffdc2626);
            bypassBtn.setColour(juce::TextButton::buttonColourId, col);
            bypassBtn.setColour(juce::TextButton::buttonOnColourId, col);
            bypassCb(index, !isActive);
        };
        addAndMakeVisible(bypassBtn);

        uiBtn.setButtonText("UI");
        uiBtn.setEnabled(false);
        uiBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e293b));
        uiBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff94a3b8));
        uiBtn.onClick = [this] { openUiCb(index); };
        addAndMakeVisible(uiBtn);

        delBtn.setButtonText("✕");
        delBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0x22ef4444));
        delBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff87171));
        delBtn.onClick = [this] { removeCb(index); };
        addAndMakeVisible(delBtn);
    }

    void updateSlotIndex(int newIndex) {
        index = newIndex;
        slotLabel.setText("FX " + juce::String(index + 1), juce::dontSendNotification);
    }

    void setPluginName(const juce::String& name) {
        if (name.isEmpty()) {
            nameBtn.setButtonText("+ Click to Insert VST3");
            nameBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff111827));
            nameBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff64748b));
            uiBtn.setEnabled(false);
            bypassBtn.setEnabled(false);
        } else {
            nameBtn.setButtonText(name);
            nameBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0c4a6e));
            nameBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff0f9ff));
            uiBtn.setEnabled(true);
            bypassBtn.setEnabled(true);
        }
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff0d1322));
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(juce::Colour(0xff1e293b));
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(3);
        slotLabel.setBounds(r.removeFromLeft(44));
        delBtn.setBounds(r.removeFromRight(26).reduced(1));
        uiBtn.setBounds(r.removeFromRight(36).reduced(1));
        bypassBtn.setBounds(r.removeFromRight(42).reduced(1));
        r.removeFromRight(6);
        nameBtn.setBounds(r.reduced(1));
    }

private:
    int index;
    std::function<void(int)> selectCb;
    std::function<void(int)> openUiCb;
    std::function<void(int, bool)> bypassCb;
    std::function<void(int)> removeCb;

    juce::Label slotLabel;
    juce::TextButton nameBtn;
    juce::TextButton bypassBtn;
    juce::TextButton uiBtn;
    juce::TextButton delBtn;
};