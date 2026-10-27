// Modbus slave (server) simulator tab.
#pragma once

#include "DataFormat.h"
#include "SlaveServer.h"

#include <map>
#include <wx/wx.h>

class ConnectionPanel;
class LogView;
class RegTable;
class wxGrid;
class wxGridEvent;
class wxSpinCtrl;
class wxNotebook;

struct SimSpec {
    enum Type { None = 0, Increment, Decrement, Random, Sine, Triangle, Sawtooth, Toggle, Count };
    Type type = None;
    DataFmt fmt = DataFmt::Unsigned16;
    WordOrder order = WordOrder::ABCD;
    double min = 0, max = 100, step = 1;
    int intervalMs = 1000; // Increment / Decrement / Random / Toggle
    int periodMs = 10000;  // Sine / Triangle / Sawtooth
    // runtime state
    double value = 0;
    long long next = 0;

    static wxArrayString TypeNames();
    wxString Describe() const;
};

using SimMap = std::map<int, SimSpec>; // key: address

class SlavePanel : public wxPanel {
public:
    explicit SlavePanel(wxWindow* parent);
    ~SlavePanel() override;

    bool IsRunning() const { return m_server != nullptr; }
    wxString GetTitle() const;

    // used by RegTable
    DataFmt CurrentFormat() const;
    WordOrder CurrentOrder() const;
    SlaveData& Data() { return m_data; }
    SimMap& Sims(int table) { return m_sims[table]; }
    std::map<int, wxString>& Names(int table) { return m_names[table]; }

private:
    void OnStart(wxCommandEvent&);
    void OnStatus(wxThreadEvent&);
    void OnRequest(wxThreadEvent&);
    void OnRefreshTimer(wxTimerEvent&);
    void OnSimTimer(wxTimerEvent&);
    void OnGridDClick(wxGridEvent&);
    void OnGridContext(wxGridEvent&);
    void OnGoto(wxCommandEvent&);
    void OnSave(wxCommandEvent&);
    void OnLoad(wxCommandEvent&);
    void OnOptionsChanged();

    void StopServer();
    void UpdateUi();
    void SetStatus(const wxString& msg, bool error = false);
    int CurrentTable() const;
    wxGrid* CurrentGrid() const;
    std::vector<int> SelectedRows(wxGrid* grid) const;
    void EditSimulation(int table, const std::vector<int>& rows);
    void SetValues(int table, const std::vector<int>& rows);
    void ApplySim(int table, int addr, SimSpec& sim, long long now);

    ConnectionPanel* m_conn;
    wxButton* m_startBtn;
    wxStaticText* m_status;
    wxStaticText* m_stats;
    wxSpinCtrl* m_unit;
    wxCheckBox* m_anyUnit;
    wxSpinCtrl* m_delay;
    wxCheckBox* m_noResp;
    wxChoice* m_exception;
    wxChoice* m_format;
    wxChoice* m_order;
    wxSpinCtrl* m_goto;
    wxCheckBox* m_simEnable;
    wxCheckBox* m_logReq;
    wxNotebook* m_book;
    wxGrid* m_grids[TBL_COUNT];
    RegTable* m_tables[TBL_COUNT];
    LogView* m_log;

    wxTimer m_refreshTimer;
    wxTimer m_simTimer;

    SlaveData m_data;
    SlaveOptions m_opts;
    SlaveServer* m_server = nullptr;
    SimMap m_sims[TBL_COUNT];
    std::map<int, wxString> m_names[TBL_COUNT];

    unsigned long m_reqCount = 0, m_errCount = 0;
    long m_clients = 0;
};
