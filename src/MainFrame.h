// Main window: tabbed container for any number of master / slave sessions.
#pragma once

#include <wx/wx.h>

class wxAuiNotebook;
class wxAuiNotebookEvent;

class MainFrame : public wxFrame {
public:
    MainFrame();

private:
    void AddMaster();
    void AddSlave();
    void OnPageClose(wxAuiNotebookEvent& ev);
    void OnClose(wxCloseEvent& ev);
    void OnAbout(wxCommandEvent&);

    wxAuiNotebook* m_book;
    int m_masterNo = 0;
    int m_slaveNo = 0;
};
