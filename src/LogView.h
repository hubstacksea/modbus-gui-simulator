// Buffered traffic / event log with pause, clear and save.
#pragma once

#include <wx/wx.h>

class LogView : public wxPanel {
public:
    explicit LogView(wxWindow* parent);

    enum Kind { Info, Tx, Rx, Error };
    void Add(Kind kind, const wxString& text);

private:
    void Flush(wxTimerEvent&);
    void OnSave(wxCommandEvent&);

    struct Line {
        Kind kind;
        wxString text;
    };

    wxTextCtrl* m_text;
    wxCheckBox* m_pause;
    wxCheckBox* m_autoScroll;
    wxTimer m_timer;
    std::vector<Line> m_pending;
    int m_lines = 0;
    static const int kMaxLines = 5000;
};
