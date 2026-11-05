#pragma once
// Small helpers to build UI elements in code (cards, labels, badges).

namespace NpuStudio::Ui
{
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;

    inline Media::Brush Brush(wchar_t const* key)
    {
        return Application::Current().Resources().Lookup(winrt::box_value(key)).as<Media::Brush>();
    }

    inline Style StyleRes(wchar_t const* key)
    {
        return Application::Current().Resources().Lookup(winrt::box_value(key)).as<Style>();
    }

    inline TextBlock Text(winrt::hstring const& text, wchar_t const* style = L"BodyTextBlockStyle")
    {
        TextBlock t;
        t.Text(text);
        t.Style(StyleRes(style));
        t.TextWrapping(TextWrapping::Wrap);
        return t;
    }

    inline TextBlock Caption(winrt::hstring const& text)
    {
        auto t = Text(text, L"CaptionTextBlockStyle");
        t.Foreground(Brush(L"TextFillColorSecondaryBrush"));
        return t;
    }

    // Small rounded label, e.g. "NPU" or "imagem".
    inline Border Badge(winrt::hstring const& text, bool accent)
    {
        Border b;
        b.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
        b.Padding(ThicknessHelper::FromLengths(6, 1, 6, 2));
        b.Background(Brush(accent ? L"AccentFillColorDefaultBrush" : L"ControlFillColorSecondaryBrush"));
        TextBlock t;
        t.Text(text);
        t.FontSize(11);
        t.Foreground(Brush(accent ? L"TextOnAccentFillColorPrimaryBrush" : L"TextFillColorSecondaryBrush"));
        b.Child(t);
        return b;
    }

    inline Border Card()
    {
        Border b;
        b.Background(Brush(L"CardBackgroundFillColorDefaultBrush"));
        b.BorderBrush(Brush(L"CardStrokeColorDefaultBrush"));
        b.BorderThickness(ThicknessHelper::FromUniformLength(1));
        b.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        b.Padding(ThicknessHelper::FromLengths(16, 12, 16, 12));
        return b;
    }

    inline FontIcon Icon(wchar_t const* glyph, double size = 16)
    {
        FontIcon i;
        i.Glyph(glyph);
        i.FontSize(size);
        return i;
    }

    // Shows a modal OK/Cancel dialog; returns true on primary.
    inline winrt::Windows::Foundation::IAsyncOperation<bool> Confirm(XamlRoot root, winrt::hstring title,
                                                                    winrt::hstring content, winrt::hstring primary)
    {
        ContentDialog d;
        d.XamlRoot(root);
        d.Title(winrt::box_value(title));
        d.Content(winrt::box_value(content));
        d.PrimaryButtonText(primary);
        d.CloseButtonText(L"Cancelar");
        d.DefaultButton(ContentDialogButton::Close);
        auto r = co_await d.ShowAsync();
        co_return r == ContentDialogResult::Primary;
    }
}
