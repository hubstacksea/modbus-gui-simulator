// Small UI helpers shared by the panels.
#pragma once

#include <wx/spinctrl.h>
#include <wx/wx.h>

// GTK3 spin buttons carry +/- buttons and need far more room than on MSW;
// a too small width triggers Gtk-CRITICAL warnings and clipped text.
inline wxSize SpinSize(wxWindow* w, int width)
{
#ifdef __WXGTK__
    return wxSize(w->FromDIP(width + 70), -1);
#else
    return wxSize(w->FromDIP(width), -1);
#endif
}

inline wxSpinCtrl* MakeSpin(wxWindow* parent, int min, int max, int value, int width = 90)
{
    return new wxSpinCtrl(parent, wxID_ANY, wxString::Format("%d", value), wxDefaultPosition,
                          SpinSize(parent, width), wxSP_ARROW_KEYS, min, max, value);
}

enum class Tone { Normal, Good, Bad, Tx, Rx };

// Status / log colours readable on both light and dark themes.
inline wxColour ToneColour(Tone t)
{
    bool dark = wxSystemSettings::GetAppearance().IsDark();
    switch (t) {
    case Tone::Good: return dark ? wxColour(110, 220, 110) : wxColour(0, 120, 0);
    case Tone::Bad: return dark ? wxColour(255, 110, 110) : wxColour(200, 0, 0);
    case Tone::Tx: return dark ? wxColour(120, 170, 255) : wxColour(0, 0, 190);
    case Tone::Rx: return dark ? wxColour(110, 220, 110) : wxColour(0, 128, 0);
    default: return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    }
}
