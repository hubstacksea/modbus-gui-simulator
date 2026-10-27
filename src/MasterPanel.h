// Modbus master (client) tab: polling, writing, diagnostics.
#pragma once

#include "DataFormat.h"
#include "MasterWorker.h"

#include <wx/wx.h>

class ConnectionPanel;
class LogView;
class wxGrid;
class wxGridEvent;
class wxSpinCtrl;
class wxGauge;

class MasterPanel : public wxPanel {
public:
    explicit MasterPanel(wxWindow* parent);
    ~MasterPanel() override;

    bool IsConnected() const { return m_worker != nullptr; }
    wxString GetTitle() const;

private:
    wxWindow* CreateReadPage(wxWindow* parent);
    wxWindow* CreateWritePage(wxWindow* parent);
    wxWindow* CreateToolsPage(wxWindow* parent);

    void OnConnect(wxCommandEvent&);
    void OnReadOnce(wxCommandEvent&);
    void OnPoll(wxCommandEvent&);
    void OnWrite(wxCommandEvent&);
    void OnReportId(wxCommandEvent&);
    void OnRaw(wxCommandEvent&);
    void OnScan(wxCommandEvent&);
    void OnStatus(wxThreadEvent&);
    void OnResult(wxThreadEvent&);
    void OnGridDClick(wxGridEvent&);
    void OnReadParamChanged();
    void OnWriteFcChanged();

    void StopWorker();
    void UpdateUi();
    bool Send(const MasterRequest& req);
    MasterRequest BuildReadRequest() const;
    void ShowData();
    void SetStatus(const wxString& msg, bool error = false);
    wxString DescribeRequest(const MasterRequest& req) const;
    DataFmt CurrentFormat() const;
    WordOrder CurrentOrder() const;
    int ReadFc() const;

    ConnectionPanel* m_conn;
    wxButton* m_connectBtn;
    wxStaticText* m_status;
    MasterWorker* m_worker = nullptr;
    bool m_connected = false;

    // read page
    wxSpinCtrl* m_unit;
    wxChoice* m_readFc;
    wxSpinCtrl* m_addr;
    wxSpinCtrl* m_qty;
    wxSpinCtrl* m_interval;
    wxChoice* m_format;
    wxChoice* m_order;
    wxCheckBox* m_oneBased;
    wxCheckBox* m_logPoll;
    wxButton* m_readBtn;
    wxButton* m_pollBtn;
    wxStaticText* m_stats;
    bool m_polling = false;

    // write page
    wxSpinCtrl* m_wUnit;
    wxChoice* m_writeFc;
    wxSpinCtrl* m_wAddr;
    wxTextCtrl* m_wValues;
    wxChoice* m_wFormat;
    wxTextCtrl* m_andMask;
    wxTextCtrl* m_orMask;
    wxSpinCtrl* m_rwReadAddr;
    wxSpinCtrl* m_rwReadQty;
    wxButton* m_writeBtn;
    wxWindow* m_maskRow;
    wxWindow* m_rwRow;

    // tools page
    wxButton* m_reportBtn;
    wxTextCtrl* m_raw;
    wxButton* m_rawBtn;
    wxSpinCtrl* m_scanFrom;
    wxSpinCtrl* m_scanTo;
    wxButton* m_scanBtn;
    wxGauge* m_scanGauge;
    wxTextCtrl* m_scanResult;
    bool m_scanning = false;

    wxGrid* m_grid;
    LogView* m_log;

    // last data
    MasterRequest m_lastReq;
    std::vector<uint16_t> m_lastRegs;
    std::vector<uint8_t> m_lastBits;
    bool m_haveData = false;
    bool m_lastOk = false;

    unsigned long m_txCount = 0, m_okCount = 0, m_errCount = 0;
};
