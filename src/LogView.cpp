#include "LogView.h"

#include "UiUtil.h"

#include <wx/datetime.h>
#include <wx/ffile.h>

LogView::LogView(wxWindow* parent) : wxPanel(parent), m_timer(this)
{
    auto* top = new wxBoxSizer(wxVERTICAL);
    auto* bar = new wxBoxSizer(wxHORIZONTAL);
    m_pause = new wxCheckBox(this, wxID_ANY, "Pause");
    m_autoScroll = new wxCheckBox(this, wxID_ANY, "Auto scroll");
    m_autoScroll->SetValue(true);
    auto* clear = new wxButton(this, wxID_ANY, "Clear");
    auto* save = new wxButton(this, wxID_ANY, "Save...");
    bar->Add(m_pause, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bar->Add(m_autoScroll, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bar->AddStretchSpacer();
    bar->Add(clear, 0, wxRIGHT, 4);
    bar->Add(save, 0);
    top->Add(bar, 0, wxEXPAND | wxALL, 2);

    m_text = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                            wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_DONTWRAP | wxHSCROLL);
    m_text->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    top->Add(m_text, 1, wxEXPAND);
    SetSizer(top);

    clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_text->Clear();
        m_lines = 0;
    });
    save->Bind(wxEVT_BUTTON, &LogView::OnSave, this);
    Bind(wxEVT_TIMER, &LogView::Flush, this);
    m_timer.Start(200);
}

void LogView::Add(Kind kind, const wxString& text)
{
    if (m_pause->GetValue())
        return;
    static const char* tags[] = {"   ", "TX ", "RX ", "ERR"};
    wxString line = wxDateTime::UNow().Format("%H:%M:%S.%l ") + tags[kind] + " " + text + "\n";
    m_pending.push_back({kind, line});
    if (m_pending.size() > (size_t)kMaxLines)
        m_pending.erase(m_pending.begin(), m_pending.begin() + (m_pending.size() - kMaxLines));
}

void LogView::Flush(wxTimerEvent&)
{
    if (m_pending.empty())
        return;
    m_text->Freeze();
    const wxColour colours[] = {ToneColour(Tone::Normal), ToneColour(Tone::Tx), ToneColour(Tone::Rx),
                                ToneColour(Tone::Bad)};
    for (const Line& l : m_pending) {
        m_text->SetDefaultStyle(wxTextAttr(colours[l.kind]));
        m_text->AppendText(l.text);
    }
    m_lines += (int)m_pending.size();
    m_pending.clear();
    if (m_lines > kMaxLines) {
        // drop the oldest 20 %
        int drop = m_lines - kMaxLines + kMaxLines / 5;
        long pos = 0;
        wxString all = m_text->GetValue();
        int count = 0;
        for (size_t i = 0; i < all.length() && count < drop; ++i) {
            if (all[i] == '\n') {
                ++count;
                pos = (long)i + 1;
            }
        }
        m_text->Remove(0, pos);
        m_lines -= count;
    }
    if (m_autoScroll->GetValue())
        m_text->ShowPosition(m_text->GetLastPosition());
    m_text->Thaw();
}

void LogView::OnSave(wxCommandEvent&)
{
    wxFileDialog dlg(this, "Save log", wxEmptyString, "modbus_log.txt",
                     "Text files (*.txt)|*.txt|All files|*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK)
        return;
    wxFFile f(dlg.GetPath(), "w");
    if (!f.IsOpened() || !f.Write(m_text->GetValue(), wxConvUTF8))
        wxMessageBox("Failed to write log file", "Error", wxICON_ERROR, this);
}
