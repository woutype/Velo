#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class LevelMeter : public juce::Component {
public:
    void setLevel(float newLevel) {
        level = newLevel;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        
        g.setColour(juce::Colour(0xff090d16));
        g.fillRoundedRectangle(bounds, 3.0f);

        float fillWidth = bounds.getWidth() * juce::jlimit(0.0f, 1.0f, level);
        if (fillWidth > 0.0f) {
            juce::ColourGradient grad(
                juce::Colour(0xff10b981), 0.0f, 0.0f,
                juce::Colour(0xffef4444), bounds.getWidth(), 0.0f,
                false
            );
            grad.addColour(0.65f, juce::Colour(0xff06b6d4));
            grad.addColour(0.85f, juce::Colour(0xfff59e0b));
            g.setGradientFill(grad);
            g.fillRoundedRectangle(0.0f, 0.0f, fillWidth, bounds.getHeight(), 3.0f);
        }

        g.setColour(juce::Colour(0xff1e293b));
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }

private:
    float level = 0.0f;
};