#include "MasterPanel.h"

#include "ConnectionPanel.h"
#include "LogView.h"
#include "UiUtil.h"

#include <wx/gauge.h>
#include <wx/grid.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>

namespace {

wxScrolledWindow* MakePage(wxWindow* parent)
{
    auto* p = new wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    p->SetScrollRate(0, 10);
    return p;
}

void AddRow(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, wxWindow* ctrl)
{
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(ctrl, 0, wxEXPAND);
}

const int kWriteFcs[] = {5, 6, 15, 16, 22, 23};

} // namespace

MasterPanel::MasterPanel(wxWindow* parent) : wxPanel(parent)
{
    auto* top = new wxBoxSizer(wxVERTICAL);

    // --- connection bar ---
    auto* connBox = new wxStaticBoxSizer(wxHORIZONTAL, this, "Connection");
    m_conn = new ConnectionPanel(connBox->GetStaticBox(), false);
    connBox->Add(m_conn, 1, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    m_connectBtn = new wxButton(connBox->GetStaticBox(), wxID_ANY, "Connect");
    connBox->Add(m_connectBtn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    top->Add(connBox, 0, wxEXPAND | wxALL, 4);

    m_status = new wxStaticText(this, wxID_ANY, "Disconnected");
    top->Add(m_status, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

    // --- main area ---
    auto* vsplit = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                        wxSP_LIVE_UPDATE | wxSP_3DSASH);
    auto* hsplit = new wxSplitterWindow(vsplit, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                        wxSP_LIVE_UPDATE | wxSP_3DSASH);

    auto* book = new wxNotebook(hsplit, wxID_ANY);
    book->AddPage(CreateReadPage(book), "Read / Poll");
    book->AddPage(CreateWritePage(book), "Write");
    book->AddPage(CreateToolsPage(book), "Tools");

    m_grid = new wxGrid(hsplit, wxID_ANY);
    m_grid->CreateGrid(0, 4);
    m_grid->SetColLabelValue(0, "Address");
    m_grid->SetColLabelValue(1, "Value");
    m_grid->SetColLabelValue(2, "Hex");
    m_grid->SetColLabelValue(3, "Binary");
    m_grid->SetColSize(0, 80);
    m_grid->SetColSize(1, 160);
    m_grid->SetColSize(2, 80);
    m_grid->SetColSize(3, 190);
    m_grid->SetRowLabelSize(50);
    m_grid->EnableEditing(false);
    m_grid->SetDefaultCellFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE)));
    m_grid->Bind(wxEVT_GRID_CELL_LEFT_DCLICK, &MasterPanel::OnGridDClick, this);

    hsplit->SplitVertically(book, m_grid, FromDIP(380));
    hsplit->SetMinimumPaneSize(200);

    m_log = new LogView(vsplit);
    vsplit->SplitHorizontally(hsplit, m_log, -180);
    vsplit->SetSashGravity(1.0);
    vsplit->SetMinimumPaneSize(80);
    top->Add(vsplit, 1, wxEXPAND | wxALL, 4);

    SetSizer(top);

    m_connectBtn->Bind(wxEVT_BUTTON, &MasterPanel::OnConnect, this);
    Bind(EVT_MASTER_STATUS, &MasterPanel::OnStatus, this);
    Bind(EVT_MASTER_RESULT, &MasterPanel::OnResult, this);

    OnReadParamChanged();
    OnWriteFcChanged();
    UpdateUi();
}

MasterPanel::~MasterPanel()
{
    StopWorker();
}

wxString MasterPanel::GetTitle() const
{
    return "Master: " + m_conn->GetSettings().Describe();
}

wxWindow* MasterPanel::CreateReadPage(wxWindow* parent)
{
    auto* p = MakePage(parent);
    auto* s = new wxBoxSizer(wxVERTICAL);
    auto* g = new wxFlexGridSizer(2, 6, 8);
    g->AddGrowableCol(1);

    m_unit = MakeSpin(p, 0, 255, 1);
    AddRow(p, g, "Unit ID:", m_unit);

    wxArrayString fcs;
    fcs.Add("01 Read Coils");
    fcs.Add("02 Read Discrete Inputs");
    fcs.Add("03 Read Holding Registers");
    fcs.Add("04 Read Input Registers");
    m_readFc = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, fcs);
    m_readFc->SetSelection(2);
    AddRow(p, g, "Function:", m_readFc);

    m_addr = MakeSpin(p, 0, 65535, 0);
    AddRow(p, g, "Start address:", m_addr);
    m_qty = MakeSpin(p, 1, 125, 10);
    AddRow(p, g, "Quantity:", m_qty);
    m_interval = MakeSpin(p, 10, 3600000, 1000);
    AddRow(p, g, "Poll interval [ms]:", m_interval);

    s->Add(g, 0, wxEXPAND | wxALL, 8);

    auto* btns = new wxBoxSizer(wxHORIZONTAL);
    m_readBtn = new wxButton(p, wxID_ANY, "Read once");
    m_pollBtn = new wxButton(p, wxID_ANY, "Start polling");
    btns->Add(m_readBtn, 1, wxRIGHT, 4);
    btns->Add(m_pollBtn, 1);
    s->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_stats = new wxStaticText(p, wxID_ANY, "Tx: 0  OK: 0  Err: 0");
    s->Add(m_stats, 0, wxALL, 8);

    auto* g2 = new wxFlexGridSizer(2, 6, 8);
    g2->AddGrowableCol(1);
    m_format = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, DataFormat::FormatNames());
    m_format->SetSelection(0);
    AddRow(p, g2, "Display format:", m_format);
    m_order = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, DataFormat::OrderNames());
    m_order->SetSelection(0);
    AddRow(p, g2, "Byte order:", m_order);
    s->Add(g2, 0, wxEXPAND | wxALL, 8);

    m_oneBased = new wxCheckBox(p, wxID_ANY, "Show 1-based (PLC) addresses");
    s->Add(m_oneBased, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    m_logPoll = new wxCheckBox(p, wxID_ANY, "Log poll traffic");
    m_logPoll->SetValue(true);
    s->Add(m_logPoll, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    s->Add(new wxStaticText(p, wxID_ANY,
                            "Double click a value in the table to write it\n(coils: toggle, holding registers: edit)."),
           0, wxALL, 8);
    p->SetSizer(s);

    m_readBtn->Bind(wxEVT_BUTTON, &MasterPanel::OnReadOnce, this);
    m_pollBtn->Bind(wxEVT_BUTTON, &MasterPanel::OnPoll, this);
    m_readFc->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { OnReadParamChanged(); });
    auto reformat = [this](wxCommandEvent&) { ShowData(); };
    m_format->Bind(wxEVT_CHOICE, reformat);
    m_order->Bind(wxEVT_CHOICE, reformat);
    m_oneBased->Bind(wxEVT_CHECKBOX, reformat);
    auto restartPoll = [this](wxSpinEvent&) {
        if (m_polling && m_worker)
            m_worker->SetPoll(true, BuildReadRequest(), m_interval->GetValue());
    };
    for (wxSpinCtrl* sc : {m_unit, m_addr, m_qty, m_interval})
        sc->Bind(wxEVT_SPINCTRL, restartPoll);
    m_readFc->Bind(wxEVT_CHOICE, [this](wxCommandEvent& e) {
        if (m_polling && m_worker)
            m_worker->SetPoll(true, BuildReadRequest(), m_interval->GetValue());
        e.Skip();
    });
    return p;
}

wxWindow* MasterPanel::CreateWritePage(wxWindow* parent)
{
    auto* p = MakePage(parent);
    auto* s = new wxBoxSizer(wxVERTICAL);
    auto* g = new wxFlexGridSizer(2, 6, 8);
    g->AddGrowableCol(1);

    m_wUnit = MakeSpin(p, 0, 255, 1);
    AddRow(p, g, "Unit ID:", m_wUnit);

    wxArrayString fcs;
    for (int fc : kWriteFcs)
        fcs.Add(wxString::Format("%02d %s", fc, ModbusUtil::FunctionName(fc)));
    m_writeFc = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, fcs);
    m_writeFc->SetSelection(3);
    AddRow(p, g, "Function:", m_writeFc);

    m_wAddr = MakeSpin(p, 0, 65535, 0);
    AddRow(p, g, "Address:", m_wAddr);

    m_wFormat = new wxChoice(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, DataFormat::FormatNames());
    m_wFormat->SetSelection(0);
    AddRow(p, g, "Value format:", m_wFormat);
    s->Add(g, 0, wxEXPAND | wxALL, 8);

    s->Add(new wxStaticText(p, wxID_ANY, "Values (comma / space separated, 0x.. hex allowed):"), 0,
           wxLEFT | wxRIGHT, 8);
    m_wValues = new wxTextCtrl(p, wxID_ANY, "0", wxDefaultPosition, wxSize(-1, 70), wxTE_MULTILINE);
    s->Add(m_wValues, 0, wxEXPAND | wxALL, 8);

    // FC22 mask
    auto* maskPanel = new wxPanel(p);
    {
        auto* ms = new wxBoxSizer(wxHORIZONTAL);
        ms->Add(new wxStaticText(maskPanel, wxID_ANY, "AND mask:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_andMask = new wxTextCtrl(maskPanel, wxID_ANY, "0xFFFF", wxDefaultPosition, wxSize(80, -1));
        ms->Add(m_andMask, 0, wxRIGHT, 8);
        ms->Add(new wxStaticText(maskPanel, wxID_ANY, "OR mask:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_orMask = new wxTextCtrl(maskPanel, wxID_ANY, "0x0000", wxDefaultPosition, wxSize(80, -1));
        ms->Add(m_orMask, 0);
        maskPanel->SetSizer(ms);
    }
    m_maskRow = maskPanel;
    s->Add(maskPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // FC23 read part
    auto* rwPanel = new wxPanel(p);
    {
        auto* rs = new wxBoxSizer(wxHORIZONTAL);
        rs->Add(new wxStaticText(rwPanel, wxID_ANY, "Read addr:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_rwReadAddr = MakeSpin(rwPanel, 0, 65535, 0, 80);
        rs->Add(m_rwReadAddr, 0, wxRIGHT, 8);
        rs->Add(new wxStaticText(rwPanel, wxID_ANY, "Qty:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_rwReadQty = MakeSpin(rwPanel, 1, 125, 1, 70);
        rs->Add(m_rwReadQty, 0);
        rwPanel->SetSizer(rs);
    }
    m_rwRow = rwPanel;
    s->Add(rwPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    m_writeBtn = new wxButton(p, wxID_ANY, "Send");
    s->Add(m_writeBtn, 0, wxEXPAND | wxALL, 8);
    s->Add(new wxStaticText(p, wxID_ANY,
                            "Coils: 0/1, on/off, true/false.\nFloat / 32 / 64-bit formats use the byte order\nselected on the Read page."),
           0, wxALL, 8);
    p->SetSizer(s);

    m_writeFc->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { OnWriteFcChanged(); });
    m_writeBtn->Bind(wxEVT_BUTTON, &MasterPanel::OnWrite, this);
    return p;
}

wxWindow* MasterPanel::CreateToolsPage(wxWindow* parent)
{
    auto* p = MakePage(parent);
    auto* s = new wxBoxSizer(wxVERTICAL);

    auto* idBox = new wxStaticBoxSizer(wxVERTICAL, p, "Report Server ID (FC17)");
    m_reportBtn = new wxButton(idBox->GetStaticBox(), wxID_ANY, "Send (uses Unit ID of Read page)");
    idBox->Add(m_reportBtn, 0, wxEXPAND | wxALL, 4);
    s->Add(idBox, 0, wxEXPAND | wxALL, 6);

    auto* rawBox = new wxStaticBoxSizer(wxVERTICAL, p, "Raw request");
    rawBox->Add(new wxStaticText(rawBox->GetStaticBox(), wxID_ANY,
                                 "Hex bytes: [unit] [function] [data...]\n(MBAP header / CRC added automatically)"),
                0, wxALL, 4);
    m_raw = new wxTextCtrl(rawBox->GetStaticBox(), wxID_ANY, "01 03 00 00 00 0A");
    rawBox->Add(m_raw, 0, wxEXPAND | wxALL, 4);
    m_rawBtn = new wxButton(rawBox->GetStaticBox(), wxID_ANY, "Send raw");
    rawBox->Add(m_rawBtn, 0, wxEXPAND | wxALL, 4);
    s->Add(rawBox, 0, wxEXPAND | wxALL, 6);

    auto* scanBox = new wxStaticBoxSizer(wxVERTICAL, p, "Unit ID scan");
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(scanBox->GetStaticBox(), wxID_ANY, "From:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_scanFrom = MakeSpin(scanBox->GetStaticBox(), 0, 255, 1, 70);
    row->Add(m_scanFrom, 0, wxRIGHT, 8);
    row->Add(new wxStaticText(scanBox->GetStaticBox(), wxID_ANY, "To:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_scanTo = MakeSpin(scanBox->GetStaticBox(), 0, 255, 247, 70);
    row->Add(m_scanTo, 0);
    scanBox->Add(row, 0, wxALL, 4);
    scanBox->Add(new wxStaticText(scanBox->GetStaticBox(), wxID_ANY,
                                  "Probes with function / address of the Read page."),
                 0, wxALL, 4);
    m_scanBtn = new wxButton(scanBox->GetStaticBox(), wxID_ANY, "Start scan");
    scanBox->Add(m_scanBtn, 0, wxEXPAND | wxALL, 4);
    m_scanGauge = new wxGauge(scanBox->GetStaticBox(), wxID_ANY, 100);
    scanBox->Add(m_scanGauge, 0, wxEXPAND | wxALL, 4);
    m_scanResult = new wxTextCtrl(scanBox->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxSize(-1, 50), wxTE_MULTILINE | wxTE_READONLY);
    scanBox->Add(m_scanResult, 1, wxEXPAND | wxALL, 4);
    s->Add(scanBox, 1, wxEXPAND | wxALL, 6);
    p->SetSizer(s);

    m_reportBtn->Bind(wxEVT_BUTTON, &MasterPanel::OnReportId, this);
    m_rawBtn->Bind(wxEVT_BUTTON, &MasterPanel::OnRaw, this);
    m_scanBtn->Bind(wxEVT_BUTTON, &MasterPanel::OnScan, this);
    return p;
}

// ---------------------------------------------------------------------------

DataFmt MasterPanel::CurrentFormat() const
{
    return (DataFmt)m_format->GetSelection();
}

WordOrder MasterPanel::CurrentOrder() const
{
    return (WordOrder)m_order->GetSelection();
}

int MasterPanel::ReadFc() const
{
    return m_readFc->GetSelection() + 1;
}

void MasterPanel::OnReadParamChanged()
{
    bool bits = ReadFc() <= 2;
    int max = bits ? MODBUS_MAX_READ_BITS : MODBUS_MAX_READ_REGISTERS;
    m_qty->SetRange(1, max);
    m_format->Enable(!bits);
    m_order->Enable(!bits);
}

void MasterPanel::OnWriteFcChanged()
{
    int fc = kWriteFcs[m_writeFc->GetSelection()];
    m_maskRow->Show(fc == 22);
    m_rwRow->Show(fc == 23);
    m_wValues->Enable(fc != 22);
    m_wFormat->Enable(fc == 6 || fc == 16 || fc == 23);
    m_maskRow->GetParent()->Layout();
}

void MasterPanel::UpdateUi()
{
    bool running = m_worker != nullptr;
    m_conn->EnableEditing(!running);
    m_connectBtn->SetLabel(running ? "Disconnect" : "Connect");
    for (wxWindow* w : {(wxWindow*)m_readBtn, (wxWindow*)m_pollBtn, (wxWindow*)m_writeBtn,
                        (wxWindow*)m_reportBtn, (wxWindow*)m_rawBtn, (wxWindow*)m_scanBtn})
        w->Enable(running);
    m_pollBtn->SetLabel(m_polling ? "Stop polling" : "Start polling");
    m_scanBtn->SetLabel(m_scanning ? "Cancel scan" : "Start scan");
}

void MasterPanel::SetStatus(const wxString& msg, bool error)
{
    m_status->SetForegroundColour(ToneColour(error ? Tone::Bad : Tone::Good));
    m_status->SetLabel(msg);
    m_status->Refresh();
}

void MasterPanel::StopWorker()
{
    if (!m_worker)
        return;
    m_worker->RequestStop();
    m_worker->Wait();
    delete m_worker;
    m_worker = nullptr;
    m_connected = false;
    m_polling = false;
    m_scanning = false;
}

void MasterPanel::OnConnect(wxCommandEvent&)
{
    if (m_worker) {
        StopWorker();
        SetStatus("Disconnected", true);
        m_log->Add(LogView::Info, "Disconnected");
        UpdateUi();
        return;
    }
    ConnSettings s = m_conn->GetSettings();
    m_worker = new MasterWorker(this, s);
    if (m_worker->Run() != wxTHREAD_NO_ERROR) {
        delete m_worker;
        m_worker = nullptr;
        SetStatus("Failed to start worker thread", true);
        return;
    }
    SetStatus("Connecting to " + s.Describe() + " ...");
    m_log->Add(LogView::Info, "Connecting to " + s.Describe());
    m_txCount = m_okCount = m_errCount = 0;
    UpdateUi();
}

void MasterPanel::OnStatus(wxThreadEvent& ev)
{
    int code = ev.GetInt();
    wxString msg = ev.GetString();
    if (code == -1) {
        // thread terminated by itself (connection failure) or after stop
        if (m_worker) {
            m_worker->Wait();
            delete m_worker;
            m_worker = nullptr;
        }
        m_connected = false;
        m_polling = false;
        m_scanning = false;
        UpdateUi();
        return;
    }
    if (msg.empty())
        return;
    m_connected = code == 1;
    SetStatus(msg, code != 1);
    m_log->Add(code == 1 ? LogView::Info : LogView::Error, msg);
}

MasterRequest MasterPanel::BuildReadRequest() const
{
    MasterRequest r;
    r.kind = MasterRequest::Read;
    r.unit = m_unit->GetValue();
    r.fc = ReadFc();
    r.addr = m_addr->GetValue();
    r.count = m_qty->GetValue();
    if (r.addr + r.count > 65536)
        r.count = 65536 - r.addr;
    return r;
}

bool MasterPanel::Send(const MasterRequest& req)
{
    if (!m_worker) {
        SetStatus("Not connected", true);
        return false;
    }
    m_worker->Post(req);
    return true;
}

void MasterPanel::OnReadOnce(wxCommandEvent&)
{
    Send(BuildReadRequest());
}

void MasterPanel::OnPoll(wxCommandEvent&)
{
    if (!m_worker)
        return;
    m_polling = !m_polling;
    m_worker->SetPoll(m_polling, BuildReadRequest(), m_interval->GetValue());
    m_log->Add(LogView::Info, m_polling ? "Polling started" : "Polling stopped");
    UpdateUi();
}

void MasterPanel::OnWrite(wxCommandEvent&)
{
    int fc = kWriteFcs[m_writeFc->GetSelection()];
    MasterRequest r;
    r.unit = m_wUnit->GetValue();
    r.fc = fc;
    r.addr = m_wAddr->GetValue();
    wxString err;
    DataFmt fmt = (DataFmt)m_wFormat->GetSelection();

    switch (fc) {
    case 5:
    case 15:
        if (!DataFormat::ParseBits(m_wValues->GetValue(), r.bits, &err)) {
            wxMessageBox(err, "Invalid input", wxICON_WARNING, this);
            return;
        }
        if (fc == 5)
            r.bits.resize(1);
        else if (r.bits.size() > MODBUS_MAX_WRITE_BITS) {
            wxMessageBox("Too many coils (max 1968)", "Invalid input", wxICON_WARNING, this);
            return;
        }
        r.kind = fc == 5 ? MasterRequest::WriteCoil : MasterRequest::WriteCoils;
        break;
    case 6:
    case 16:
    case 23:
        if (!DataFormat::ParseList(m_wValues->GetValue(), fmt, CurrentOrder(), r.regs, &err)) {
            wxMessageBox(err, "Invalid input", wxICON_WARNING, this);
            return;
        }
        if (fc == 6) {
            if (r.regs.size() != 1) {
                wxMessageBox("FC06 writes exactly one 16-bit register.\nUse FC16 for 32/64-bit values.",
                             "Invalid input", wxICON_WARNING, this);
                return;
            }
            r.kind = MasterRequest::WriteRegister;
        } else if (fc == 16) {
            if (r.regs.size() > MODBUS_MAX_WRITE_REGISTERS) {
                wxMessageBox("Too many registers (max 123)", "Invalid input", wxICON_WARNING, this);
                return;
            }
            r.kind = MasterRequest::WriteRegisters;
        } else {
            if (r.regs.size() > MODBUS_MAX_WR_WRITE_REGISTERS) {
                wxMessageBox("Too many registers (max 121)", "Invalid input", wxICON_WARNING, this);
                return;
            }
            r.kind = MasterRequest::ReadWrite;
            r.writeAddr = r.addr;
            r.addr = m_rwReadAddr->GetValue();
            r.count = m_rwReadQty->GetValue();
        }
        break;
    case 22: {
        long long a, o;
        if (!DataFormat::ParseInt(m_andMask->GetValue(), a) || !DataFormat::ParseInt(m_orMask->GetValue(), o) ||
            a < 0 || a > 0xFFFF || o < 0 || o > 0xFFFF) {
            wxMessageBox("Invalid mask value", "Invalid input", wxICON_WARNING, this);
            return;
        }
        r.kind = MasterRequest::MaskWrite;
        r.andMask = (uint16_t)a;
        r.orMask = (uint16_t)o;
        break;
    }
    }
    Send(r);
}

void MasterPanel::OnReportId(wxCommandEvent&)
{
    MasterRequest r;
    r.kind = MasterRequest::ReportSlaveId;
    r.unit = m_unit->GetValue();
    r.fc = 17;
    Send(r);
}

void MasterPanel::OnRaw(wxCommandEvent&)
{
    MasterRequest r;
    if (!DataFormat::ParseHexBytes(m_raw->GetValue(), r.raw) || r.raw.size() < 2) {
        wxMessageBox("Enter at least unit id and function code as hex bytes.", "Invalid input",
                     wxICON_WARNING, this);
        return;
    }
    if (r.raw.size() > MODBUS_MAX_PDU_LENGTH + 1) {
        wxMessageBox("Request too long", "Invalid input", wxICON_WARNING, this);
        return;
    }
    r.kind = MasterRequest::Raw;
    r.unit = r.raw[0];
    r.fc = r.raw[1];
    Send(r);
}

void MasterPanel::OnScan(wxCommandEvent&)
{
    if (!m_worker)
        return;
    if (m_scanning) {
        m_worker->CancelScan();
        return;
    }
    MasterRequest r = BuildReadRequest();
    r.kind = MasterRequest::Scan;
    r.scanFrom = m_scanFrom->GetValue();
    r.scanTo = m_scanTo->GetValue();
    if (r.scanTo < r.scanFrom) {
        wxMessageBox("'To' must be >= 'From'", "Invalid input", wxICON_WARNING, this);
        return;
    }
    if (m_polling) {
        m_polling = false;
        m_worker->SetPoll(false);
    }
    m_scanGauge->SetRange(r.scanTo - r.scanFrom + 1);
    m_scanGauge->SetValue(0);
    m_scanResult->Clear();
    m_scanning = true;
    m_log->Add(LogView::Info, wxString::Format("Scanning unit IDs %d..%d", r.scanFrom, r.scanTo));
    Send(r);
    UpdateUi();
}

wxString MasterPanel::DescribeRequest(const MasterRequest& req) const
{
    wxString s = wxString::Format("Unit %d FC%02d %s", req.unit, req.fc, ModbusUtil::FunctionName(req.fc));
    switch (req.kind) {
    case MasterRequest::Read:
        s += wxString::Format(" addr=%d qty=%d", req.addr, req.count);
        break;
    case MasterRequest::WriteCoil:
    case MasterRequest::WriteCoils: {
        s += wxString::Format(" addr=%d qty=%d [", req.addr, (int)req.bits.size());
        for (size_t i = 0; i < req.bits.size() && i < 32; ++i)
            s += req.bits[i] ? '1' : '0';
        s += req.bits.size() > 32 ? "...]" : "]";
        break;
    }
    case MasterRequest::WriteRegister:
    case MasterRequest::WriteRegisters: {
        s += wxString::Format(" addr=%d qty=%d [", req.addr, (int)req.regs.size());
        for (size_t i = 0; i < req.regs.size() && i < 16; ++i)
            s += wxString::Format(i ? " %04X" : "%04X", req.regs[i]);
        s += req.regs.size() > 16 ? " ...]" : "]";
        break;
    }
    case MasterRequest::MaskWrite:
        s += wxString::Format(" addr=%d and=0x%04X or=0x%04X", req.addr, req.andMask, req.orMask);
        break;
    case MasterRequest::ReadWrite:
        s += wxString::Format(" write addr=%d qty=%d, read addr=%d qty=%d", req.writeAddr,
                              (int)req.regs.size(), req.addr, req.count);
        break;
    case MasterRequest::Raw:
        s = "Raw: " + DataFormat::HexDump(req.raw.data(), (int)req.raw.size());
        break;
    default:
        break;
    }
    return s;
}

void MasterPanel::OnResult(wxThreadEvent& ev)
{
    MasterResultPtr r = ev.GetPayload<MasterResultPtr>();
    if (!r)
        return;
    const MasterRequest& req = r->req;

    // --- scan ---
    if (req.kind == MasterRequest::Scan) {
        if (r->scanProgress) {
            m_scanGauge->SetValue(std::min(m_scanGauge->GetRange(), r->scanCurrent - req.scanFrom + 1));
            if (r->ok) {
                m_scanResult->AppendText(wxString::Format("ID %d: %s (%.1f ms)\n", r->scanCurrent, r->error, r->ms));
                m_log->Add(LogView::Rx, wxString::Format("Scan: unit %d responded (%s)", r->scanCurrent, r->error));
            }
        } else {
            m_scanning = false;
            wxString ids;
            for (int id : r->scanFound)
                ids += wxString::Format("%d ", id);
            m_log->Add(LogView::Info, wxString::Format("Scan finished: %d device(s) found %s",
                                                       (int)r->scanFound.size(), ids));
            SetStatus(wxString::Format("Scan finished: %d device(s) found", (int)r->scanFound.size()));
            UpdateUi();
        }
        return;
    }

    ++m_txCount;
    bool logIt = !req.poll || m_logPoll->GetValue();
    if (logIt)
        m_log->Add(LogView::Tx, DescribeRequest(req));

    if (!r->ok) {
        ++m_errCount;
        if (logIt)
            m_log->Add(LogView::Error, wxString::Format("%s (%.1f ms)", r->error, r->ms));
        SetStatus(wxString::Format("Error: %s", r->error), true);
        if (req.kind == MasterRequest::Read && req.poll) {
            m_lastOk = false;
            if (m_grid->GetNumberRows() > 0)
                m_grid->SetCellTextColour(0, 1, ToneColour(Tone::Bad));
        }
    } else {
        ++m_okCount;
        wxString detail;
        switch (req.kind) {
        case MasterRequest::Read:
        case MasterRequest::ReadWrite: {
            m_lastReq = req;
            if (req.kind == MasterRequest::ReadWrite)
                m_lastReq.fc = 3;
            m_lastRegs = r->regs;
            m_lastBits = r->bits;
            m_haveData = true;
            m_lastOk = true;
            ShowData();
            if (!r->bits.empty()) {
                for (size_t i = 0; i < r->bits.size() && i < 32; ++i)
                    detail += r->bits[i] ? '1' : '0';
                if (r->bits.size() > 32)
                    detail += "...";
            } else {
                for (size_t i = 0; i < r->regs.size() && i < 16; ++i)
                    detail += wxString::Format(i ? " %04X" : "%04X", r->regs[i]);
                if (r->regs.size() > 16)
                    detail += " ...";
            }
            break;
        }
        case MasterRequest::ReportSlaveId: {
            detail = DataFormat::HexDump(r->raw.data(), (int)r->raw.size());
            if (r->raw.size() >= 2) {
                wxString ascii;
                for (size_t i = 2; i < r->raw.size(); ++i)
                    ascii += (r->raw[i] >= 0x20 && r->raw[i] < 0x7F) ? (wxChar)r->raw[i] : '.';
                detail += wxString::Format("  [id=0x%02X run=%s \"%s\"]", r->raw[0],
                                           r->raw[1] == 0xFF ? "ON" : "OFF", ascii);
            }
            break;
        }
        case MasterRequest::Raw:
            detail = "Response: " + DataFormat::HexDump(r->raw.data(), (int)r->raw.size());
            break;
        default:
            detail = "Write OK";
            break;
        }
        if (logIt)
            m_log->Add(LogView::Rx, wxString::Format("(%.1f ms) %s", r->ms, detail));
        SetStatus(wxString::Format("OK  %s  (%.1f ms)", ModbusUtil::FunctionName(req.fc), r->ms));
    }
    m_stats->SetLabel(wxString::Format("Tx: %lu  OK: %lu  Err: %lu  Last: %.1f ms", m_txCount, m_okCount,
                                       m_errCount, r->ms));
}

void MasterPanel::ShowData()
{
    if (!m_haveData)
        return;
    bool bits = m_lastReq.fc <= 2;
    int n = bits ? (int)m_lastBits.size() : (int)m_lastRegs.size();
    int base = m_oneBased->GetValue() ? 1 : 0;

    m_grid->BeginBatch();
    if (m_grid->GetNumberRows() != n) {
        if (m_grid->GetNumberRows() > 0)
            m_grid->DeleteRows(0, m_grid->GetNumberRows());
        if (n > 0)
            m_grid->AppendRows(n);
    }
    DataFmt fmt = CurrentFormat();
    WordOrder order = CurrentOrder();
    int regsPer = DataFormat::RegisterCount(fmt);
    const wxColour defFg = m_grid->GetDefaultCellTextColour();
    const wxColour defBg = m_grid->GetDefaultCellBackgroundColour();
    for (int i = 0; i < n; ++i) {
        int addr = m_lastReq.addr + i;
        m_grid->SetRowLabelValue(i, wxString::Format("%d", i));
        m_grid->SetCellValue(i, 0, wxString::Format("%d", addr + base));
        if (bits) {
            m_grid->SetCellValue(i, 1, m_lastBits[i] ? "1 (ON)" : "0 (OFF)");
            m_grid->SetCellTextColour(i, 1, m_lastBits[i] ? *wxBLACK : defFg);
            m_grid->SetCellBackgroundColour(i, 1, m_lastBits[i] ? wxColour(190, 250, 190) : defBg);
            m_grid->SetCellValue(i, 2, wxEmptyString);
            m_grid->SetCellValue(i, 3, wxEmptyString);
        } else {
            uint16_t v = m_lastRegs[i];
            if (i % regsPer == 0)
                m_grid->SetCellValue(i, 1, DataFormat::Format(&m_lastRegs[i], n - i, fmt, order));
            else
                m_grid->SetCellValue(i, 1, wxEmptyString);
            m_grid->SetCellTextColour(i, 1, defFg);
            m_grid->SetCellBackgroundColour(i, 1, defBg);
            m_grid->SetCellValue(i, 2, wxString::Format("0x%04X", v));
            m_grid->SetCellValue(i, 3, DataFormat::ToBinary16(v));
        }
    }
    m_grid->EndBatch();
}

void MasterPanel::OnGridDClick(wxGridEvent& ev)
{
    if (!m_haveData || !m_worker)
        return;
    int row = ev.GetRow();
    int fc = m_lastReq.fc;
    if (fc == 2 || fc == 4) {
        SetStatus("Discrete inputs / input registers are read-only", true);
        return;
    }
    MasterRequest r;
    r.unit = m_lastReq.unit;
    if (fc == 1) {
        if (row >= (int)m_lastBits.size())
            return;
        r.kind = MasterRequest::WriteCoil;
        r.fc = 5;
        r.addr = m_lastReq.addr + row;
        r.bits.push_back(m_lastBits[row] ? 0 : 1);
        Send(r);
        Send(BuildReadRequest());
        return;
    }
    DataFmt fmt = CurrentFormat();
    int regsPer = DataFormat::RegisterCount(fmt);
    int start = row - row % regsPer;
    if (start >= (int)m_lastRegs.size())
        return;
    wxString cur = DataFormat::Format(&m_lastRegs[start], (int)m_lastRegs.size() - start, fmt, CurrentOrder());
    wxTextEntryDialog dlg(this,
                          wxString::Format("New value for address %d (%s):", m_lastReq.addr + start,
                                           DataFormat::FormatNames()[(int)fmt]),
                          "Write register", cur);
    if (dlg.ShowModal() != wxID_OK)
        return;
    std::vector<uint16_t> regs;
    if (!DataFormat::Parse(dlg.GetValue(), fmt, CurrentOrder(), regs)) {
        wxMessageBox("Invalid value", "Write register", wxICON_WARNING, this);
        return;
    }
    r.addr = m_lastReq.addr + start;
    r.regs = regs;
    if (regs.size() == 1) {
        r.kind = MasterRequest::WriteRegister;
        r.fc = 6;
    } else {
        r.kind = MasterRequest::WriteRegisters;
        r.fc = 16;
    }
    Send(r);
    Send(BuildReadRequest());
}
