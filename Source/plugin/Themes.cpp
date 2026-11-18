#include "Themes.h"

using namespace juce;

Colour Theme::dustColour (float semis) const
{
    return dustLow.interpolatedWith (dustHigh, jlimit (0.0f, 1.0f, (semis + 12.0f) / 36.0f));
}

const std::vector<Theme>& allThemes()
{
    static const std::vector<Theme> t = [] {
        std::vector<Theme> v;
        auto add = [&] (const char* name, uint32 bg, uint32 bg2, uint32 panel, uint32 edge, uint32 text, uint32 dim,
                        uint32 accent, uint32 accent2, uint32 knob, uint32 track, uint32 lo, uint32 hi, int style, bool glow, bool grain)
        {
            v.push_back ({ name, Colour (bg), Colour (bg2), Colour (panel), Colour (edge), Colour (text), Colour (dim),
                           Colour (accent), Colour (accent2), Colour (knob), Colour (track), Colour (lo), Colour (hi), style, glow, grain });
        };
        //   name          bg          bg2         panel       edge        text        dim         accent      accent2     knob        track       dust low    dust high
        add ("Lantern",    0xff120c08, 0xff1d140d, 0xff21170f, 0xff4a3220, 0xfff4e6d0, 0xff9c8468, 0xffffb347, 0xffff6f3c, 0xff2e2015, 0xff2c2016, 0xffff7a2e, 0xfffff0b0, 0, true,  false);
        add ("Moth",       0xffe9e2d4, 0xffddd4c2, 0xfff4efe4, 0xffbfb39a, 0xff2b2620, 0xff7a6f5e, 0xff7a5c8f, 0xffa86b4b, 0xff3a3530, 0xffd9d0bd, 0xff6d4d7d, 0xffc08a5a, 2, false, true);
        add ("Frost",      0xff0a1220, 0xff0f1a2c, 0xff121f35, 0xff2a4466, 0xffe8f2ff, 0xff7f97b8, 0xff8fd8ff, 0xffd7c8ff, 0xff1b2a44, 0xff1c2b45, 0xff5d8cff, 0xffeaffff, 0, true,  false);
        add ("Neon Dust",  0xff07050c, 0xff100a1a, 0xff0f0a18, 0xff3b1f58, 0xfff6ecff, 0xff8c7aa8, 0xffff3fb4, 0xff2ee6ff, 0xff17112a, 0xff241a3a, 0xffff3fb4, 0xff2ee6ff, 3, true,  false);
        return v;
    }();
    return t;
}

const Theme& themeByName (const String& name)
{
    for (auto& t : allThemes()) if (t.name == name) return t;
    return allThemes().front();
}

DustLookAndFeel::DustLookAndFeel() { setTheme (allThemes().front()); }

void DustLookAndFeel::setTheme (const Theme& t)
{
    th = &t;
    setColour (ComboBox::backgroundColourId, t.bg2);
    setColour (ComboBox::textColourId, t.text);
    setColour (ComboBox::outlineColourId, t.edge);
    setColour (ComboBox::arrowColourId, t.dim);
    setColour (PopupMenu::backgroundColourId, t.bg2);
    setColour (PopupMenu::textColourId, t.text);
    setColour (PopupMenu::headerTextColourId, t.accent);
    setColour (PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.35f));
    setColour (PopupMenu::highlightedTextColourId, t.text);
    setColour (TextButton::textColourOffId, t.text);
    setColour (TextButton::textColourOnId, t.text);
    setColour (TextButton::buttonColourId, t.bg2);
    setColour (Label::textColourId, t.text);
    setColour (TooltipWindow::backgroundColourId, t.bg2);
    setColour (TooltipWindow::textColourId, t.text);
    setColour (BubbleComponent::backgroundColourId, t.bg2);
    setColour (BubbleComponent::outlineColourId, t.edge);
    setColour (Slider::textBoxTextColourId, t.text);
    setColour (AlertWindow::backgroundColourId, t.bg2);
    setColour (AlertWindow::textColourId, t.text);
}

Font DustLookAndFeel::getComboBoxFont (ComboBox&) { return Font (FontOptions (13.0f)); }
Font DustLookAndFeel::getPopupMenuFont() { return Font (FontOptions (14.0f)); }

void DustLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto& t = *th;
    auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float d = jmin (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float r = d * 0.5f;
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getProperties()["bipolar"];
    const float from = bipolar ? (start + end) * 0.5f : start;
    const Colour col = s.getProperties().contains ("colour") ? Colour ((uint32) (int64) s.getProperties()["colour"]) : t.accent;
    const bool enabled = s.isEnabled();

    auto arc = [&] (float a0, float a1, float radius, float thick, Colour cc)
    {
        Path p;
        p.addCentredArc (c.x, c.y, radius, radius, 0, jmin (a0, a1), jmax (a0, a1), true);
        g.setColour (cc);
        g.strokePath (p, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
    };

    switch (t.knobStyle)
    {
        case 1:   // pointer knob
        {
            g.setColour (Colours::black.withAlpha (0.25f));
            g.fillEllipse (Rectangle<float> (d * 0.82f, d * 0.82f).withCentre (c.translated (1.5f, 2.5f)));
            for (int i = 0; i <= 10; ++i)
            {
                const float a = start + (end - start) * i / 10.0f;
                g.setColour (t.dim);
                g.drawLine (Line<float> (c.getPointOnCircumference (r * 0.86f, a), c.getPointOnCircumference (r * 0.98f, a)), 1.2f);
            }
            g.setColour (t.knob);
            g.fillEllipse (Rectangle<float> (d * 0.58f, d * 0.58f).withCentre (c));
            Path ptr;
            ptr.addTriangle (-r * 0.16f, 0, r * 0.16f, 0, 0, -r * 0.80f);
            ptr.addEllipse (-r * 0.22f, -r * 0.22f, r * 0.44f, r * 0.44f);
            g.fillPath (ptr, AffineTransform::rotation (angle).translated (c));
            g.setColour (t.text);
            g.drawLine (Line<float> (c.getPointOnCircumference (r * 0.25f, angle), c.getPointOnCircumference (r * 0.72f, angle)), 2.0f);
            break;
        }
        case 2:   // flat dot knob (Moth)
        {
            arc (start, end, r * 0.9f, 2.0f, t.dim.withAlpha (0.4f));
            arc (from, angle, r * 0.9f, 2.5f, enabled ? col : t.dim);
            g.setColour (t.knob);
            g.fillEllipse (Rectangle<float> (d * 0.66f, d * 0.66f).withCentre (c));
            g.setColour (t.knob.contrasting (0.8f));
            g.fillEllipse (Rectangle<float> (d * 0.1f, d * 0.1f).withCentre (c.getPointOnCircumference (r * 0.48f, angle)));
            break;
        }
        case 3:   // LED ring (Neon Dust)
        {
            const int leds = 21;
            for (int i = 0; i < leds; ++i)
            {
                const float a = start + (end - start) * i / (float) (leds - 1);
                const bool lit = (a >= jmin (from, angle) - 0.001f && a <= jmax (from, angle) + 0.001f);
                const auto p = c.getPointOnCircumference (r * 0.88f, a);
                if (lit && enabled)
                {
                    g.setColour (col.withAlpha (0.25f));
                    g.fillEllipse (Rectangle<float> (d * 0.13f, d * 0.13f).withCentre (p));
                }
                g.setColour (lit && enabled ? col : t.track);
                g.fillEllipse (Rectangle<float> (d * 0.065f, d * 0.065f).withCentre (p));
            }
            g.setColour (t.knob);
            g.fillEllipse (Rectangle<float> (d * 0.6f, d * 0.6f).withCentre (c));
            g.setColour (t.edge);
            g.drawEllipse (Rectangle<float> (d * 0.6f, d * 0.6f).withCentre (c), 1.0f);
            g.setColour (enabled ? col : t.dim);
            g.drawLine (Line<float> (c.getPointOnCircumference (r * 0.1f, angle), c.getPointOnCircumference (r * 0.55f, angle)), 2.0f);
            break;
        }
        default:  // arc ring (Lantern / Frost)
        {
            arc (start, end, r * 0.86f, 4.0f, t.track);
            if (t.glow && enabled) arc (from, angle, r * 0.86f, 10.0f, col.withAlpha (0.16f));
            arc (from, angle, r * 0.86f, 4.0f, enabled ? col : t.dim);
            auto body = Rectangle<float> (d * 0.62f, d * 0.62f).withCentre (c);
            g.setGradientFill (ColourGradient (t.knob.brighter (0.25f), body.getX(), body.getY(), t.knob.darker (0.3f), body.getRight(), body.getBottom(), false));
            g.fillEllipse (body);
            g.setColour (t.edge);
            g.drawEllipse (body, 1.0f);
            g.setColour (t.text);
            g.drawLine (Line<float> (c.getPointOnCircumference (r * 0.12f, angle), c.getPointOnCircumference (r * 0.5f, angle)), 2.0f);
            break;
        }
    }
}

void DustLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    const auto& t = *th;
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const Colour col = b.getProperties().contains ("colour") ? Colour ((uint32) (int64) b.getProperties()["colour"]) : t.accent;
    g.setColour (on ? col.withAlpha (0.9f) : t.bg2);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (highlighted ? col : t.edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (on ? col.contrasting (0.85f) : t.dim);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText (b.getButtonText(), r, Justification::centred);
}

void DustLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    const auto& t = *th;
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? t.accent.withAlpha (0.4f) : t.bg2);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (highlighted ? t.accent : t.edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void DustLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    const auto& t = *th;
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (t.bg2);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) ? t.accent : t.edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (t.dim);
    g.fillPath (arrow);
}
