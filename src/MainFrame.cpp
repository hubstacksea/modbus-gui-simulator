#include "MainFrame.h"

#include "AppIconData.h"

#include "MasterPanel.h"
#include "ModbusUtil.h"
#include "SlavePanel.h"

#include <wx/aboutdlg.h>
#include <wx/aui/auibook.h>
#include <wx/iconbndl.h>
#include <wx/mstream.h>

namespace {
enum { ID_NEW_MASTER = wxID_HIGHEST + 1, ID_NEW_SLAVE, ID_CLOSE_TAB };
}

MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "Modbus GUI - Master / Slave Simulator")
{
    wxIconBundle icons;
    for (const auto& e : AppIconData::entries) {
        wxMemoryInputStream in(e.data, e.size);
        wxImage img(in, wxBITMAP_TYPE_PNG);
        if (img.IsOk()) {
            wxIcon icon;
            icon.CopyFromBitmap(wxBitmap(img));
            icons.AddIcon(icon);
        }
    }
    SetIcons(icons);

    auto* file = new wxMenu;
    file->Append(ID_NEW_MASTER, "New &Master (client)\tCtrl+M");
    file->Append(ID_NEW_SLAVE, "New &Slave (server simulator)\tCtrl+L");
    file->AppendSeparator();
    file->Append(ID_CLOSE_TAB, "&Close tab\tCtrl+W");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit\tAlt+F4");
    auto* help = new wxMenu;
    help->Append(wxID_ABOUT, "&About");
    auto* bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(help, "&Help");
    SetMenuBar(bar);
    CreateStatusBar();
    SetStatusText(ModbusUtil::LibVersion());

    auto* tb = CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT | wxTB_NOICONS);
    tb->AddTool(ID_NEW_MASTER, "+ Master", wxNullBitmap, "Open a new master (client) tab");
    tb->AddTool(ID_NEW_SLAVE, "+ Slave", wxNullBitmap, "Open a new slave (server simulator) tab");
    tb->Realize();

    m_book = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxAUI_NB_DEFAULT_STYLE | wxAUI_NB_CLOSE_ON_ALL_TABS);

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AddMaster(); }, ID_NEW_MASTER);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AddSlave(); }, ID_NEW_SLAVE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        int sel = m_book->GetSelection();
        if (sel != wxNOT_FOUND)
            m_book->DeletePage(sel);
    }, ID_CLOSE_TAB);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    m_book->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE, &MainFrame::OnPageClose, this);

    AddMaster();
    AddSlave();
    m_book->SetSelection(0);

    SetSize(FromDIP(wxSize(1250, 860)));
    SetMinSize(FromDIP(wxSize(800, 600)));
    Centre();
}

void MainFrame::AddMaster()
{
    auto* p = new MasterPanel(m_book);
    m_book->AddPage(p, wxString::Format("Master %d", ++m_masterNo), true);
}

void MainFrame::AddSlave()
{
    auto* p = new SlavePanel(m_book);
    m_book->AddPage(p, wxString::Format("Slave %d", ++m_slaveNo), true);
}

void MainFrame::OnPageClose(wxAuiNotebookEvent& ev)
{
    wxWindow* page = m_book->GetPage(ev.GetSelection());
    bool active = false;
    if (auto* m = dynamic_cast<MasterPanel*>(page))
        active = m->IsConnected();
    else if (auto* s = dynamic_cast<SlavePanel*>(page))
        active = s->IsRunning();
    if (active && wxMessageBox("This session is active. Close it anyway?", "Close tab",
                               wxYES_NO | wxICON_QUESTION, this) != wxYES)
        ev.Veto();
}

void MainFrame::OnClose(wxCloseEvent& ev)
{
    // Destroy panels explicitly so that worker threads are joined before exit.
    while (m_book->GetPageCount())
        m_book->DeletePage(0);
    ev.Skip();
}

void MainFrame::OnAbout(wxCommandEvent&)
{
    wxAboutDialogInfo info;
    info.SetName("Modbus GUI");
    info.SetVersion(MODBUSGUI_VERSION);
    info.SetDescription("Modbus TCP / RTU master tool and slave simulator.\n\n" + ModbusUtil::LibVersion() + "\n" +
                        wxVERSION_STRING);
    info.SetIcon(GetIcons().GetIcon(FromDIP(wxSize(64, 64))));
    info.SetCopyright("libmodbus is licensed under LGPL-2.1-or-later");
    wxAboutBox(info, this);
}
