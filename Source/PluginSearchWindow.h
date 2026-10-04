#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

class PluginSearchComponent : public juce::Component, 
                             public juce::ListBoxModel, 
                             public juce::TextEditor::Listener {
public:
    PluginSearchComponent(const juce::KnownPluginList& fullList, 
                          std::function<void(const juce::PluginDescription&)> onSelect,
                          std::function<void()> onClose)
        : selectCallback(onSelect), closeCallback(onClose) {
        
        allPlugins = fullList.getTypes();
        filteredPlugins = allPlugins;

        searchBox.setTextToShowWhenEmpty("Search VST3 (e.g. Pro-Q, Waves, CLA, Reverb)...", juce::Colour(0xff64748b));
        searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0b1120));
        searchBox.setColour(juce::TextEditor::textColourId, juce::Colour(0xfff8fafc));
        searchBox.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff38bdf8));
        searchBox.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff06b6d4));
        searchBox.setFont(juce::FontOptions(14.0f));
        searchBox.addListener(this);
        addAndMakeVisible(searchBox);

        listBox.setModel(this);
        listBox.setRowHeight(40);
        listBox.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff070b14));
        listBox.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff1e293b));
        addAndMakeVisible(listBox);

        countLabel.setFont(juce::FontOptions(11.0f));
        countLabel.setColour(juce::Label::textColourId, juce::Colour(0xff94a3b8));
        updateCountLabel();
        addAndMakeVisible(countLabel);

        setSize(460, 560);
        searchBox.grabKeyboardFocus();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14);
        searchBox.setBounds(area.removeFromTop(38));
        area.removeFromTop(8);
        countLabel.setBounds(area.removeFromTop(18));
        area.removeFromTop(6);
        listBox.setBounds(area);
    }

    int getNumRows() override {
        return filteredPlugins.size();
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override {
        if (!juce::isPositiveAndBelow(rowNumber, filteredPlugins.size()))
            return;

        auto bounds = juce::Rectangle<float>(4.0f, 2.0f, (float)width - 8.0f, (float)height - 4.0f);

        if (rowIsSelected) {
            g.setColour(juce::Colour(0xff0369a1));
            g.fillRoundedRectangle(bounds, 5.0f);
            g.setColour(juce::Colour(0xff38bdf8));
            g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
        } else if (rowNumber % 2 == 1) {
            g.setColour(juce::Colour(0x221e293b));
            g.fillRoundedRectangle(bounds, 4.0f);
        }

        auto& desc = filteredPlugins.getReference(rowNumber);

        g.setColour(rowIsSelected ? juce::Colour(0xffffffff) : juce::Colour(0xfff1f5f9));
        g.setFont(juce::FontOptions(13.5f, juce::Font::bold));
        g.drawText(desc.name, 16, 2, width - 180, height - 4, juce::Justification::centredLeft, true);

        g.setColour(rowIsSelected ? juce::Colour(0xffbae6fd) : juce::Colour(0xff64748b));
        g.setFont(juce::FontOptions(11.5f));
        g.drawText(desc.manufacturerName, width - 170, 2, 150, height - 4, juce::Justification::centredRight, true);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override {
        selectRowAndClose(row);
    }

    void textEditorTextChanged(juce::TextEditor&) override {
        juce::String q = searchBox.getText().trim();
        filteredPlugins.clear();

        if (q.isEmpty()) {
            filteredPlugins = allPlugins;
        } else {
            for (auto& p : allPlugins) {
                if (p.name.containsIgnoreCase(q) || p.manufacturerName.containsIgnoreCase(q) || p.category.containsIgnoreCase(q)) {
                    filteredPlugins.add(p);
                }
            }
        }

        updateCountLabel();
        listBox.updateContent();
        listBox.repaint();
        if (!filteredPlugins.isEmpty())
            listBox.selectRow(0);
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::returnKey) {
            selectRowAndClose(listBox.getSelectedRow());
            return true;
        }
        if (key == juce::KeyPress::escapeKey) {
            if (closeCallback) closeCallback();
            return true;
        }
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
            return listBox.keyPressed(key);
        }
        return false;
    }

private:
    void updateCountLabel() {
        countLabel.setText("Available plugins: " + juce::String(filteredPlugins.size()) + 
                           " of " + juce::String(allPlugins.size()), juce::dontSendNotification);
    }

    void selectRowAndClose(int row) {
        if (juce::isPositiveAndBelow(row, filteredPlugins.size())) {
            auto chosen = filteredPlugins.getReference(row);
            if (selectCallback)
                selectCallback(chosen);
        }
    }

    juce::TextEditor searchBox;
    juce::ListBox listBox;
    juce::Label countLabel;

    juce::Array<juce::PluginDescription> allPlugins;
    juce::Array<juce::PluginDescription> filteredPlugins;
    std::function<void(const juce::PluginDescription&)> selectCallback;
    std::function<void()> closeCallback;
};

class PluginSearchWindow : public juce::DocumentWindow {
public:
    PluginSearchWindow(const juce::KnownPluginList& fullList, 
                       std::function<void(const juce::PluginDescription&)> onSelect)
        : DocumentWindow("Add VST3 Effect", juce::Colour(0xff090d16), DocumentWindow::closeButton) {
        
        setUsingNativeTitleBar(true);
        auto* comp = new PluginSearchComponent(fullList, [this, onSelect](const juce::PluginDescription& d) {
            onSelect(d);
            delete this;
        }, [this] {
            delete this;
        });

        setContentOwned(comp, true);
        setResizable(false, false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override {
        delete this;
    }
};