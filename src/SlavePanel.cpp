#include "SlavePanel.h"

#include "ConnectionPanel.h"
#include "LogView.h"
#include "UiUtil.h"

#include <cmath>
#include <random>
#include <wx/ffile.h>
#include <wx/grid.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>

namespace {

const char* kTableNames[TBL_COUNT] = {"Coils (0x)", "Discrete Inputs (1x)", "Input Registers (3x)",
                                      "Holding Registers (4x)"};
// notebook page order -> table kind
const int kPageTable[TBL_COUNT] = {TBL_COILS, TBL_DISCRETE, TBL_INPUT, TBL_HOLDING};
const char* kTableKeys[TBL_COUNT] = {"coil", "di", "hr", "ir"};

bool IsBitTable(int t)
{
    return t == TBL_COILS || t == TBL_DISCRETE;
}

std::mt19937& Rng()
{
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}

long long NowMs()
{
    return wxGetLocalTimeMillis().GetValue();
}

} // namespace

// ---------------------------------------------------------------------------
wxArrayString SimSpec::TypeNames()
{
    wxArrayString a;
    a.Add("None");
    a.Add("Increment");
    a.Add("Decrement");
    a.Add("Random");
    a.Add("Sine");
    a.Add("Triangle");
    a.Add("Sawtooth");
    a.Add("Toggle");
    return a;
}

wxString SimSpec::Describe() const
{
    if (type == None)
        return wxEmptyString;
    wxString name = TypeNames()[type];
    switch (type) {
    case Increment:
    case Decrement:
        return wxString::Format("%s %g..%g step %g /%dms", name, min, max, step, intervalMs);
    case Random:
        return wxString::Format("%s %g..%g /%dms", name, min, max, intervalMs);
    case Toggle:
        return wxString::Format("%s /%dms", name, intervalMs);
    default:
        return wxString::Format("%s %g..%g T=%dms", name, min, max, periodMs);
    }
}

// ---------------------------------------------------------------------------
// Virtual grid table on top of SlaveData (no copy of 65536 rows).
class RegTable : public wxGridTableBase {
public:
    enum Col { COL_VALUE = 0, COL_HEX, COL_BIN, COL_SIM, COL_NAME };

    RegTable(SlavePanel* panel, int table) : m_panel(panel), m_table(table)
    {
        m_roAttr = new wxGridCellAttr;
        m_roAttr->SetReadOnly(true);
        m_roAttr->SetTextColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        m_changedAttr = new wxGridCellAttr;
        m_changedAttr->SetBackgroundColour(wxColour(255, 240, 150));
        m_changedAttr->SetTextColour(*wxBLACK);
        m_onAttr = new wxGridCellAttr;
        m_onAttr->SetBackgroundColour(wxColour(190, 250, 190));
        m_onAttr->SetTextColour(*wxBLACK);
        m_onAttr->SetReadOnly(true);
        m_offAttr = new wxGridCellAttr;
        m_offAttr->SetReadOnly(true);
        m_simAttr = new wxGridCellAttr;
        m_simAttr->SetReadOnly(true);
        m_simAttr->SetTextColour(wxColour(60, 120, 230));
    }
    ~RegTable() override
    {
        m_roAttr->DecRef();
        m_changedAttr->DecRef();
        m_onAttr->DecRef();
        m_offAttr->DecRef();
        m_simAttr->DecRef();
    }

    bool Bits() const { return IsBitTable(m_table); }
    int ColKind(int col) const
    {
        if (Bits())
            return col == 0 ? COL_VALUE : (col == 1 ? COL_SIM : COL_NAME);
        return col;
    }

    int GetNumberRows() override { return 65536; }
    int GetNumberCols() override { return Bits() ? 3 : 5; }
    bool IsEmptyCell(int, int) override { return false; }

    wxString GetColLabelValue(int col) override
    {
        switch (ColKind(col)) {
        case COL_VALUE: return "Value";
        case COL_HEX: return "Hex";
        case COL_BIN: return "Binary";
        case COL_SIM: return "Simulation";
        default: return "Name / Comment";
        }
    }

    wxString GetRowLabelValue(int row) override { return wxString::Format("%d", row); }

    wxString GetValue(int row, int col) override
    {
        int kind = ColKind(col);
        if (kind == COL_SIM) {
            auto& sims = m_panel->Sims(m_table);
            auto it = sims.find(row);
            return it == sims.end() ? wxString() : it->second.Describe();
        }
        if (kind == COL_NAME) {
            auto& names = m_panel->Names(m_table);
            auto it = names.find(row);
            return it == names.end() ? wxString() : it->second;
        }
        SlaveData& d = m_panel->Data();
        wxMutexLocker lock(d.mutex);
        if (Bits())
            return d.GetBit(m_table, row) ? "1" : "0";
        uint16_t v = d.GetReg(m_table, row);
        if (kind == COL_HEX)
            return wxString::Format("%04X", v);
        if (kind == COL_BIN)
            return DataFormat::ToBinary16(v);
        DataFmt fmt = m_panel->CurrentFormat();
        int n = DataFormat::RegisterCount(fmt);
        if (row % n)
            return wxEmptyString;
        uint16_t regs[4] = {0, 0, 0, 0};
        for (int i = 0; i < n && row + i < 65536; ++i)
            regs[i] = d.GetReg(m_table, row + i);
        return DataFormat::Format(regs, std::min(n, 65536 - row), fmt, m_panel->CurrentOrder());
    }

    void SetValue(int row, int col, const wxString& value) override
    {
        int kind = ColKind(col);
        if (kind == COL_NAME) {
            if (value.empty())
                m_panel->Names(m_table).erase(row);
            else
                m_panel->Names(m_table)[row] = value;
            return;
        }
        SlaveData& d = m_panel->Data();
        if (Bits()) {
            wxString v = value.Lower();
            wxMutexLocker lock(d.mutex);
            d.SetBit(m_table, row, (v == "1" || v == "on" || v == "true") ? 1 : 0);
            return;
        }
        std::vector<uint16_t> regs;
        bool ok;
        if (kind == COL_HEX)
            ok = DataFormat::Parse(value, DataFmt::Hex16, WordOrder::ABCD, regs);
        else
            ok = DataFormat::Parse(value, m_panel->CurrentFormat(), m_panel->CurrentOrder(), regs);
        if (!ok) {
            wxBell();
            return;
        }
        wxMutexLocker lock(d.mutex);
        for (size_t i = 0; i < regs.size() && row + (int)i < 65536; ++i)
            d.SetReg(m_table, row + (int)i, regs[i]);
    }

    wxGridCellAttr* GetAttr(int row, int col, wxGridCellAttr::wxAttrKind) override
    {
        int kind = ColKind(col);
        wxGridCellAttr* a = nullptr;
        if (kind == COL_SIM) {
            a = m_simAttr;
        } else if (kind == COL_BIN) {
            a = m_roAttr;
        } else if (kind == COL_VALUE || kind == COL_HEX) {
            SlaveData& d = m_panel->Data();
            long changed;
            uint8_t bit = 0;
            {
                wxMutexLocker lock(d.mutex);
                changed = d.changedAt[m_table][row];
                if (Bits())
                    bit = d.GetBit(m_table, row);
            }
            long now = (long)wxGetLocalTimeMillis().GetLo();
            if (changed && (unsigned long)(now - changed) < 1500)
                a = m_changedAttr;
            else if (Bits())
                a = bit ? m_onAttr : m_offAttr;
            else if (kind == COL_VALUE && row % DataFormat::RegisterCount(m_panel->CurrentFormat()))
                a = m_roAttr;
        }
        if (a)
            a->IncRef();
        return a;
    }

private:
    SlavePanel* m_panel;
    int m_table;
    wxGridCellAttr* m_roAttr;
    wxGridCellAttr* m_changedAttr;
    wxGridCellAttr* m_onAttr;
    wxGridCellAttr* m_offAttr;
    wxGridCellAttr* m_simAttr;
};

// ---------------------------------------------------------------------------
// Simulation settings dialog
class SimDialog : public wxDialog {
public:
    SimDialog(wxWindow* parent, bool bits, const SimSpec& init) : wxDialog(parent, wxID_ANY, "Simulation")
    {
        auto* s = new wxBoxSizer(wxVERTICAL);
        auto* g = new wxFlexGridSizer(2, 6, 8);
        g->AddGrowableCol(1);
        auto add = [&](const wxString& label, wxWindow* w) {
            g->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
            g->Add(w, 1, wxEXPAND);
        };
        wxArrayString types = SimSpec::TypeNames();
        if (bits) {
            wxArrayString b;
            b.Add(types[SimSpec::None]);
            b.Add(types[SimSpec::Random]);
            b.Add(types[SimSpec::Toggle]);
            types = b;
        }
        m_bits = bits;
        m_type = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, types);
        int sel = 0;
        if (bits)
            sel = init.type == SimSpec::Random ? 1 : (init.type == SimSpec::Toggle ? 2 : 0);
        else
            sel = init.type;
        m_type->SetSelection(sel);
        add("Type:", m_type);
        m_fmt = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, DataFormat::FormatNames());
        m_fmt->SetSelection((int)init.fmt);
        m_order = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, DataFormat::OrderNames());
        m_order->SetSelection((int)init.order);
        m_min = new wxTextCtrl(this, wxID_ANY, wxString::FromCDouble(init.min));
        m_max = new wxTextCtrl(this, wxID_ANY, wxString::FromCDouble(init.max));
        m_step = new wxTextCtrl(this, wxID_ANY, wxString::FromCDouble(init.step));
        m_interval = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                    wxSP_ARROW_KEYS, 50, 3600000, init.intervalMs);
        m_period = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                  wxSP_ARROW_KEYS, 100, 36000000, init.periodMs);
        if (!bits) {
            add("Value format:", m_fmt);
            add("Byte order:", m_order);
            add("Minimum:", m_min);
            add("Maximum:", m_max);
            add("Step:", m_step);
        } else {
            m_fmt->Hide();
            m_order->Hide();
            m_min->Hide();
            m_max->Hide();
            m_step->Hide();
        }
        add("Update interval [ms]:", m_interval);
        if (!bits)
            add("Period [ms] (wave):", m_period);
        else
            m_period->Hide();
        s->Add(g, 1, wxEXPAND | wxALL, 10);
        s->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 8);
        SetSizerAndFit(s);
    }

    bool Get(SimSpec& out) const
    {
        int sel = m_type->GetSelection();
        if (m_bits) {
            static const SimSpec::Type map[] = {SimSpec::None, SimSpec::Random, SimSpec::Toggle};
            out.type = map[sel];
        } else {
            out.type = (SimSpec::Type)sel;
        }
        out.fmt = (DataFmt)m_fmt->GetSelection();
        out.order = (WordOrder)m_order->GetSelection();
        if (!m_bits) {
            if (!m_min->GetValue().ToCDouble(&out.min) || !m_max->GetValue().ToCDouble(&out.max) ||
                !m_step->GetValue().ToCDouble(&out.step))
                return false;
            if (out.max < out.min)
                std::swap(out.min, out.max);
        }
        out.intervalMs = m_interval->GetValue();
        out.periodMs = m_period->GetValue();
        out.value = out.type == SimSpec::Decrement ? out.max : out.min;
        out.next = 0;
        return true;
    }

private:
    bool m_bits;
    wxChoice* m_type;
    wxChoice* m_fmt;
    wxChoice* m_order;
    wxTextCtrl* m_min;
    wxTextCtrl* m_max;
    wxTextCtrl* m_step;
    wxSpinCtrl* m_interval;
    wxSpinCtrl* m_period;
};

// ---------------------------------------------------------------------------
SlavePanel::SlavePanel(wxWindow* parent) : wxPanel(parent), m_refreshTimer(this), m_simTimer(this)
{
    auto* top = new wxBoxSizer(wxVERTICAL);

    auto* connBox = new wxStaticBoxSizer(wxHORIZONTAL, this, "Server");
    m_conn = new ConnectionPanel(connBox->GetStaticBox(), true);
    connBox->Add(m_conn, 1, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    m_startBtn = new wxButton(connBox->GetStaticBox(), wxID_ANY, "Start");
    connBox->Add(m_startBtn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    top->Add(connBox, 0, wxEXPAND | wxALL, 4);

    // options
    auto* optBox = new wxStaticBoxSizer(wxHORIZONTAL, this, "Behaviour");
    wxWindow* ob = optBox->GetStaticBox();
    optBox->Add(new wxStaticText(ob, wxID_ANY, "Unit ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_unit = new wxSpinCtrl(ob, wxID_ANY, "1", wxDefaultPosition, SpinSize(ob, 70), wxSP_ARROW_KEYS, 1, 247, 1);
    optBox->Add(m_unit, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    m_anyUnit = new wxCheckBox(ob, wxID_ANY, "Any unit (TCP)");
    m_anyUnit->SetValue(true);
    optBox->Add(m_anyUnit, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    optBox->Add(new wxStaticText(ob, wxID_ANY, "Delay [ms]:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_delay = new wxSpinCtrl(ob, wxID_ANY, "0", wxDefaultPosition, SpinSize(ob, 80), wxSP_ARROW_KEYS, 0, 60000, 0);
    optBox->Add(m_delay, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    m_noResp = new wxCheckBox(ob, wxID_ANY, "No response");
    optBox->Add(m_noResp, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    optBox->Add(new wxStaticText(ob, wxID_ANY, "Force exception:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    wxArrayString exc;
    exc.Add("None");
    exc.Add("01 Illegal function");
    exc.Add("02 Illegal data address");
    exc.Add("03 Illegal data value");
    exc.Add("04 Server device failure");
    exc.Add("05 Acknowledge");
    exc.Add("06 Server device busy");
    exc.Add("08 Memory parity error");
    exc.Add("0A Gateway path unavailable");
    exc.Add("0B Gateway target failed");
    m_exception = new wxChoice(ob, wxID_ANY, wxDefaultPosition, wxDefaultSize, exc);
    m_exception->SetSelection(0);
    optBox->Add(m_exception, 0, wxALIGN_CENTER_VERTICAL);
    top->Add(optBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);

    auto* stRow = new wxBoxSizer(wxHORIZONTAL);
    m_status = new wxStaticText(this, wxID_ANY, "Stopped");
    m_stats = new wxStaticText(this, wxID_ANY, wxEmptyString);
    stRow->Add(m_status, 1, wxALIGN_CENTER_VERTICAL);
    stRow->Add(m_stats, 0, wxALIGN_CENTER_VERTICAL);
    top->Add(stRow, 0, wxEXPAND | wxALL, 6);

    // view bar
    auto* view = new wxBoxSizer(wxHORIZONTAL);
    view->Add(new wxStaticText(this, wxID_ANY, "Format:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_format = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, DataFormat::FormatNames());
    m_format->SetSelection(0);
    view->Add(m_format, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_order = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, DataFormat::OrderNames());
    m_order->SetSelection(0);
    view->Add(m_order, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    view->Add(new wxStaticText(this, wxID_ANY, "Go to:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_goto = new wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, SpinSize(this, 90),
                            wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER, 0, 65535, 0);
    view->Add(m_goto, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
    auto* gotoBtn = new wxButton(this, wxID_ANY, "Go", wxDefaultPosition, wxSize(40, -1));
    view->Add(gotoBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    m_simEnable = new wxCheckBox(this, wxID_ANY, "Run simulation");
    m_simEnable->SetValue(true);
    view->Add(m_simEnable, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    m_logReq = new wxCheckBox(this, wxID_ANY, "Log requests");
    m_logReq->SetValue(true);
    view->Add(m_logReq, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    view->AddStretchSpacer();
    auto* loadBtn = new wxButton(this, wxID_ANY, "Load...");
    auto* saveBtn = new wxButton(this, wxID_ANY, "Save...");
    view->Add(loadBtn, 0, wxRIGHT, 4);
    view->Add(saveBtn, 0);
    top->Add(view, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

    auto* split = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxSP_LIVE_UPDATE | wxSP_3DSASH);
    m_book = new wxNotebook(split, wxID_ANY);
    for (int page = 0; page < TBL_COUNT; ++page) {
        int t = kPageTable[page];
        auto* grid = new wxGrid(m_book, wxID_ANY);
        m_tables[t] = new RegTable(this, t);
        grid->SetTable(m_tables[t], true, wxGrid::wxGridSelectRows);
        grid->SetRowLabelSize(60);
        grid->SetDefaultCellFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE)));
        if (IsBitTable(t)) {
            grid->SetColSize(0, 70);
            grid->SetColSize(1, 220);
            grid->SetColSize(2, 260);
        } else {
            grid->SetColSize(0, 170);
            grid->SetColSize(1, 60);
            grid->SetColSize(2, 190);
            grid->SetColSize(3, 220);
            grid->SetColSize(4, 220);
        }
        grid->DisableDragRowSize();
        grid->Bind(wxEVT_GRID_CELL_LEFT_DCLICK, &SlavePanel::OnGridDClick, this);
        grid->Bind(wxEVT_GRID_CELL_RIGHT_CLICK, &SlavePanel::OnGridContext, this);
        grid->Bind(wxEVT_GRID_LABEL_RIGHT_CLICK, &SlavePanel::OnGridContext, this);
        m_grids[t] = grid;
        m_book->AddPage(grid, kTableNames[page]);
    }
    m_log = new LogView(split);
    split->SplitHorizontally(m_book, m_log, -170);
    split->SetSashGravity(1.0);
    split->SetMinimumPaneSize(80);
    top->Add(split, 1, wxEXPAND | wxALL, 4);
    SetSizer(top);

    top->Add(new wxStaticText(this, wxID_ANY,
                              "Double click: edit value (bits toggle).  Right click: set value / simulation for "
                              "the selected rows.  Yellow = recently written by a master."),
             0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    m_startBtn->Bind(wxEVT_BUTTON, &SlavePanel::OnStart, this);
    Bind(EVT_SLAVE_STATUS, &SlavePanel::OnStatus, this);
    Bind(EVT_SLAVE_REQUEST, &SlavePanel::OnRequest, this);
    Bind(wxEVT_TIMER, &SlavePanel::OnRefreshTimer, this, m_refreshTimer.GetId());
    Bind(wxEVT_TIMER, &SlavePanel::OnSimTimer, this, m_simTimer.GetId());
    gotoBtn->Bind(wxEVT_BUTTON, &SlavePanel::OnGoto, this);
    m_goto->Bind(wxEVT_TEXT_ENTER, &SlavePanel::OnGoto, this);
    saveBtn->Bind(wxEVT_BUTTON, &SlavePanel::OnSave, this);
    loadBtn->Bind(wxEVT_BUTTON, &SlavePanel::OnLoad, this);
    auto refresh = [this](wxCommandEvent&) {
        for (wxGrid* g : m_grids)
            g->ForceRefresh();
    };
    m_format->Bind(wxEVT_CHOICE, refresh);
    m_order->Bind(wxEVT_CHOICE, refresh);
    m_unit->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { OnOptionsChanged(); });
    m_delay->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { OnOptionsChanged(); });
    for (wxCheckBox* c : {m_anyUnit, m_noResp})
        c->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { OnOptionsChanged(); });
    m_exception->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { OnOptionsChanged(); });

    OnOptionsChanged();
    m_refreshTimer.Start(300);
    m_simTimer.Start(100);
    UpdateUi();
}

SlavePanel::~SlavePanel()
{
    m_refreshTimer.Stop();
    m_simTimer.Stop();
    StopServer();
}

wxString SlavePanel::GetTitle() const
{
    return "Slave: " + m_conn->GetSettings().Describe();
}

DataFmt SlavePanel::CurrentFormat() const
{
    return (DataFmt)m_format->GetSelection();
}

WordOrder SlavePanel::CurrentOrder() const
{
    return (WordOrder)m_order->GetSelection();
}

int SlavePanel::CurrentTable() const
{
    int page = m_book->GetSelection();
    return kPageTable[page < 0 ? 0 : page];
}

wxGrid* SlavePanel::CurrentGrid() const
{
    return m_grids[CurrentTable()];
}

void SlavePanel::OnOptionsChanged()
{
    static const int codes[] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 11};
    m_opts.unitId = m_unit->GetValue();
    m_opts.anyUnit = m_anyUnit->GetValue();
    m_opts.delayMs = m_delay->GetValue();
    m_opts.noResponse = m_noResp->GetValue();
    m_opts.forceException = codes[m_exception->GetSelection()];
}

void SlavePanel::SetStatus(const wxString& msg, bool error)
{
    m_status->SetForegroundColour(ToneColour(error ? Tone::Bad : Tone::Good));
    m_status->SetLabel(msg);
    m_status->Refresh();
}

void SlavePanel::UpdateUi()
{
    bool running = m_server != nullptr;
    m_conn->EnableEditing(!running);
    m_startBtn->SetLabel(running ? "Stop" : "Start");
}

void SlavePanel::StopServer()
{
    if (!m_server)
        return;
    m_server->RequestStop();
    m_server->Wait();
    delete m_server;
    m_server = nullptr;
}

void SlavePanel::OnStart(wxCommandEvent&)
{
    if (m_server) {
        StopServer();
        SetStatus("Stopped", true);
        m_log->Add(LogView::Info, "Server stopped");
        m_clients = 0;
        UpdateUi();
        return;
    }
    ConnSettings s = m_conn->GetSettings();
    OnOptionsChanged();
    m_server = new SlaveServer(this, s, &m_data, &m_opts);
    if (m_server->Run() != wxTHREAD_NO_ERROR) {
        delete m_server;
        m_server = nullptr;
        SetStatus("Failed to start server thread", true);
        return;
    }
    m_reqCount = m_errCount = 0;
    SetStatus("Starting " + s.Describe() + " ...");
    UpdateUi();
}

void SlavePanel::OnStatus(wxThreadEvent& ev)
{
    int code = ev.GetInt();
    if (code == -1) {
        if (m_server) {
            m_server->Wait();
            delete m_server;
            m_server = nullptr;
        }
        m_clients = 0;
        UpdateUi();
        return;
    }
    wxString msg = ev.GetString();
    m_clients = ev.GetExtraLong();
    if (code == 0) {
        SetStatus(msg, true);
        m_log->Add(LogView::Error, msg);
    } else {
        if (code == 1)
            SetStatus(msg);
        m_log->Add(LogView::Info, msg);
    }
}

void SlavePanel::OnRequest(wxThreadEvent& ev)
{
    SlaveRequestInfoPtr info = ev.GetPayload<SlaveRequestInfoPtr>();
    if (!info)
        return;
    ++m_reqCount;
    if (info->result == SlaveRequestInfo::Failed)
        ++m_errCount;
    if (!m_logReq->GetValue())
        return;

    if (info->result == SlaveRequestInfo::Failed && info->frame.empty()) {
        m_log->Add(LogView::Error, info->peer + ": " + info->error);
        return;
    }
    wxString line = wxString::Format("[%s] Unit %d FC%02d %s", info->peer, info->unit, info->fc,
                                     ModbusUtil::FunctionName(info->fc));
    if (info->addr >= 0)
        line += wxString::Format(" addr=%d qty=%d", info->addr, info->count);
    if (info->writeAddr >= 0)
        line += wxString::Format(" write addr=%d qty=%d", info->writeAddr, info->writeCount);
    if (!info->frame.empty())
        line += "  | " + DataFormat::HexDump(info->frame.data(), (int)std::min<size_t>(info->frame.size(), 64)) +
                (info->frame.size() > 64 ? " ..." : "");
    m_log->Add(LogView::Rx, line);

    switch (info->result) {
    case SlaveRequestInfo::Replied:
        break;
    case SlaveRequestInfo::Exception:
        m_log->Add(LogView::Tx, wxString::Format("Exception %02X", info->exception));
        break;
    case SlaveRequestInfo::Ignored:
        m_log->Add(LogView::Info, "Ignored (" + info->error + ")");
        break;
    case SlaveRequestInfo::NoResponse:
        m_log->Add(LogView::Info, "No response (simulated)");
        break;
    case SlaveRequestInfo::Failed:
        m_log->Add(LogView::Error, "Reply failed: " + info->error);
        break;
    }
}

void SlavePanel::OnRefreshTimer(wxTimerEvent&)
{
    if (!IsShownOnScreen())
        return;
    CurrentGrid()->ForceRefresh();
    m_stats->SetLabel(wxString::Format("Clients: %ld   Requests: %lu   Errors: %lu", m_clients, m_reqCount,
                                       m_errCount));
    m_stats->GetParent()->Layout();
}

void SlavePanel::ApplySim(int table, int addr, SimSpec& sim, long long now)
{
    bool bits = IsBitTable(table);
    bool timeBased = sim.type == SimSpec::Sine || sim.type == SimSpec::Triangle || sim.type == SimSpec::Sawtooth;
    if (!timeBased) {
        if (now < sim.next)
            return;
        sim.next = now + sim.intervalMs;
    }
    double range = sim.max - sim.min;
    double v = sim.value;
    switch (sim.type) {
    case SimSpec::Increment:
        v += sim.step;
        if (v > sim.max)
            v = sim.min;
        break;
    case SimSpec::Decrement:
        v -= sim.step;
        if (v < sim.min)
            v = sim.max;
        break;
    case SimSpec::Random:
        if (bits)
            v = Rng()() & 1;
        else {
            std::uniform_real_distribution<double> dist(sim.min, sim.max);
            v = dist(Rng());
        }
        break;
    case SimSpec::Toggle:
        v = v ? 0 : 1;
        break;
    case SimSpec::Sine:
    case SimSpec::Triangle:
    case SimSpec::Sawtooth: {
        double ph = std::fmod((double)now, (double)sim.periodMs) / sim.periodMs; // 0..1
        double w;
        if (sim.type == SimSpec::Sine)
            w = 0.5 + 0.5 * std::sin(2 * 3.14159265358979323846 * ph);
        else if (sim.type == SimSpec::Triangle)
            w = ph < 0.5 ? ph * 2 : 2 - ph * 2;
        else
            w = ph;
        v = sim.min + range * w;
        break;
    }
    default:
        return;
    }
    sim.value = v;

    if (bits) {
        m_data.SetBit(table, addr, v != 0);
        return;
    }
    std::vector<uint16_t> regs;
    DataFormat::FromDouble(v, sim.fmt, sim.order, regs);
    for (size_t i = 0; i < regs.size() && addr + (int)i < 65536; ++i)
        m_data.SetReg(table, addr + (int)i, regs[i]);
}

void SlavePanel::OnSimTimer(wxTimerEvent&)
{
    if (!m_simEnable->GetValue())
        return;
    long long now = NowMs();
    wxMutexLocker lock(m_data.mutex);
    for (int t = 0; t < TBL_COUNT; ++t)
        for (auto& kv : m_sims[t])
            ApplySim(t, kv.first, kv.second, now);
}

std::vector<int> SlavePanel::SelectedRows(wxGrid* grid) const
{
    std::vector<int> rows;
    wxArrayInt sel = grid->GetSelectedRows();
    for (int r : sel)
        rows.push_back(r);
    if (rows.empty()) {
        wxGridCellCoordsArray tl = grid->GetSelectionBlockTopLeft();
        wxGridCellCoordsArray br = grid->GetSelectionBlockBottomRight();
        for (size_t i = 0; i < tl.size() && i < br.size(); ++i)
            for (int r = tl[i].GetRow(); r <= br[i].GetRow(); ++r)
                rows.push_back(r);
    }
    if (rows.empty() && grid->GetGridCursorRow() >= 0)
        rows.push_back(grid->GetGridCursorRow());
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

void SlavePanel::OnGridDClick(wxGridEvent& ev)
{
    int t = CurrentTable();
    if (IsBitTable(t) && ev.GetCol() == 0) {
        {
            wxMutexLocker lock(m_data.mutex);
            m_data.SetBit(t, ev.GetRow(), !m_data.GetBit(t, ev.GetRow()));
        }
        CurrentGrid()->ForceRefresh();
        return;
    }
    if (ev.GetCol() == (IsBitTable(t) ? 1 : 3)) {
        EditSimulation(t, {ev.GetRow()});
        return;
    }
    ev.Skip(); // default: open the cell editor
}

void SlavePanel::OnGridContext(wxGridEvent& ev)
{
    wxGrid* grid = CurrentGrid();
    int t = CurrentTable();
    if (ev.GetRow() >= 0) {
        std::vector<int> sel = SelectedRows(grid);
        if (std::find(sel.begin(), sel.end(), ev.GetRow()) == sel.end()) {
            grid->SelectRow(ev.GetRow());
            grid->SetGridCursor(ev.GetRow(), 0);
        }
    }
    std::vector<int> rows = SelectedRows(grid);
    if (rows.empty())
        return;

    enum { ID_SETVAL = wxID_HIGHEST + 100, ID_SIM, ID_CLEARSIM, ID_ZERO, ID_RANDOM, ID_ON, ID_OFF };
    wxMenu menu;
    menu.SetTitle(wxString::Format("%d row(s) from %d", (int)rows.size(), rows.front()));
    menu.Append(ID_SETVAL, "Set value...");
    if (IsBitTable(t)) {
        menu.Append(ID_ON, "Set ON");
        menu.Append(ID_OFF, "Set OFF");
    } else {
        menu.Append(ID_ZERO, "Set to zero");
        menu.Append(ID_RANDOM, "Fill random");
    }
    menu.AppendSeparator();
    menu.Append(ID_SIM, "Simulation...");
    menu.Append(ID_CLEARSIM, "Clear simulation");

    int id = grid->GetPopupMenuSelectionFromUser(menu);
    switch (id) {
    case ID_SETVAL:
        SetValues(t, rows);
        break;
    case ID_SIM:
        EditSimulation(t, rows);
        break;
    case ID_CLEARSIM:
        for (int r : rows)
            m_sims[t].erase(r);
        break;
    case ID_ZERO:
    case ID_OFF:
    case ID_ON:
    case ID_RANDOM: {
        wxMutexLocker lock(m_data.mutex);
        for (int r : rows) {
            if (IsBitTable(t))
                m_data.SetBit(t, r, id == ID_ON);
            else
                m_data.SetReg(t, r, id == ID_RANDOM ? (uint16_t)(Rng()() & 0xFFFF) : 0);
        }
        break;
    }
    default:
        break;
    }
    grid->ForceRefresh();
}

void SlavePanel::SetValues(int table, const std::vector<int>& rows)
{
    bool bits = IsBitTable(table);
    DataFmt fmt = CurrentFormat();
    wxTextEntryDialog dlg(this,
                          bits ? wxString("Value for selected bits (0/1):")
                               : wxString::Format("Value for selected registers (%s):", DataFormat::FormatNames()[(int)fmt]),
                          "Set value", "0");
    if (dlg.ShowModal() != wxID_OK)
        return;
    if (bits) {
        std::vector<uint8_t> b;
        if (!DataFormat::ParseBits(dlg.GetValue(), b)) {
            wxMessageBox("Invalid value", "Set value", wxICON_WARNING, this);
            return;
        }
        wxMutexLocker lock(m_data.mutex);
        for (int r : rows)
            m_data.SetBit(table, r, b[0]);
        return;
    }
    std::vector<uint16_t> regs;
    if (!DataFormat::Parse(dlg.GetValue(), fmt, CurrentOrder(), regs)) {
        wxMessageBox("Invalid value", "Set value", wxICON_WARNING, this);
        return;
    }
    int n = (int)regs.size();
    wxMutexLocker lock(m_data.mutex);
    for (int r : rows) {
        if (r % n)
            continue; // multi register values start on aligned rows
        for (int i = 0; i < n && r + i < 65536; ++i)
            m_data.SetReg(table, r + i, regs[i]);
    }
}

void SlavePanel::EditSimulation(int table, const std::vector<int>& rows)
{
    if (rows.empty())
        return;
    SimSpec init;
    auto it = m_sims[table].find(rows.front());
    if (it != m_sims[table].end())
        init = it->second;
    else {
        init.fmt = CurrentFormat();
        init.order = CurrentOrder();
        if (IsBitTable(table))
            init.type = SimSpec::Toggle;
    }
    SimDialog dlg(this, IsBitTable(table), init);
    if (dlg.ShowModal() != wxID_OK)
        return;
    SimSpec spec;
    if (!dlg.Get(spec)) {
        wxMessageBox("Invalid number", "Simulation", wxICON_WARNING, this);
        return;
    }
    int n = IsBitTable(table) ? 1 : DataFormat::RegisterCount(spec.fmt);
    for (int r : rows) {
        if (spec.type == SimSpec::None) {
            m_sims[table].erase(r);
            continue;
        }
        if (r % n)
            continue;
        if (r + n > 65536)
            continue;
        m_sims[table][r] = spec;
        // remove simulations overlapping the multi register value
        for (int i = 1; i < n; ++i)
            m_sims[table].erase(r + i);
    }
    CurrentGrid()->ForceRefresh();
}

void SlavePanel::OnGoto(wxCommandEvent&)
{
    wxGrid* g = CurrentGrid();
    int row = m_goto->GetValue();
    g->SetGridCursor(row, 0);
    g->MakeCellVisible(row, 0);
    g->SetFocus();
}

// ---------------------------------------------------------------------------
// File format (CSV, '#' comments):
//   val,<table>,<address>,<raw value>
//   name,<table>,<address>,<text>
//   sim,<table>,<address>,<type>,<fmt>,<order>,<min>,<max>,<step>,<interval>,<period>
void SlavePanel::OnSave(wxCommandEvent&)
{
    wxFileDialog dlg(this, "Save simulator data", wxEmptyString, "modbus_slave.csv",
                     "CSV files (*.csv)|*.csv|All files|*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK)
        return;
    wxString out = "# modbus_gui slave data v1\n";
    {
        wxMutexLocker lock(m_data.mutex);
        for (int t = 0; t < TBL_COUNT; ++t) {
            for (int a = 0; a < 65536; ++a) {
                int v = IsBitTable(t) ? m_data.GetBit(t, a) : m_data.GetReg(t, a);
                if (v)
                    out += wxString::Format("val,%s,%d,%d\n", kTableKeys[t], a, v);
            }
        }
    }
    for (int t = 0; t < TBL_COUNT; ++t) {
        for (auto& kv : m_names[t]) {
            wxString n = kv.second;
            n.Replace("\n", " ");
            out += wxString::Format("name,%s,%d,%s\n", kTableKeys[t], kv.first, n);
        }
        for (auto& kv : m_sims[t]) {
            const SimSpec& s = kv.second;
            out += wxString::Format("sim,%s,%d,%d,%d,%d,%s,%s,%s,%d,%d\n", kTableKeys[t], kv.first, (int)s.type,
                                    (int)s.fmt, (int)s.order, wxString::FromCDouble(s.min),
                                    wxString::FromCDouble(s.max), wxString::FromCDouble(s.step), s.intervalMs,
                                    s.periodMs);
        }
    }
    wxFFile f(dlg.GetPath(), "w");
    if (!f.IsOpened() || !f.Write(out, wxConvUTF8)) {
        wxMessageBox("Failed to write file", "Error", wxICON_ERROR, this);
        return;
    }
    m_log->Add(LogView::Info, "Saved " + dlg.GetPath());
}

void SlavePanel::OnLoad(wxCommandEvent&)
{
    wxFileDialog dlg(this, "Load simulator data", wxEmptyString, wxEmptyString,
                     "CSV files (*.csv)|*.csv|All files|*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK)
        return;
    wxTextFile file;
    if (!file.Open(dlg.GetPath(), wxConvUTF8)) {
        wxMessageBox("Failed to open file", "Error", wxICON_ERROR, this);
        return;
    }
    auto tableOf = [](const wxString& k) {
        for (int t = 0; t < TBL_COUNT; ++t)
            if (k == kTableKeys[t])
                return t;
        return -1;
    };
    int lines = 0, bad = 0;
    m_data.mutex.Lock();
    for (int t = 0; t < TBL_COUNT; ++t) {
        m_sims[t].clear();
        m_names[t].clear();
    }
    memset(m_data.map->tab_bits, 0, 65536);
    memset(m_data.map->tab_input_bits, 0, 65536);
    memset(m_data.map->tab_registers, 0, 65536 * sizeof(uint16_t));
    memset(m_data.map->tab_input_registers, 0, 65536 * sizeof(uint16_t));

    for (wxString line = file.GetFirstLine(); !file.Eof(); line = file.GetNextLine()) {
        line.Trim(true).Trim(false);
        if (line.empty() || line.StartsWith("#"))
            continue;
        wxArrayString f = wxSplit(line, ',', '\0');
        if (f.size() < 4) {
            ++bad;
            continue;
        }
        int t = tableOf(f[1]);
        long addr;
        if (t < 0 || !f[2].ToLong(&addr) || addr < 0 || addr > 65535) {
            ++bad;
            continue;
        }
        if (f[0] == "val") {
            long v;
            if (!f[3].ToLong(&v)) {
                ++bad;
                continue;
            }
            if (IsBitTable(t))
                m_data.SetBit(t, addr, v != 0);
            else
                m_data.SetReg(t, addr, (uint16_t)v);
        } else if (f[0] == "name") {
            // the name may itself contain commas
            wxString n = line.AfterFirst(',').AfterFirst(',').AfterFirst(',');
            m_names[t][addr] = n;
        } else if (f[0] == "sim" && f.size() >= 11) {
            SimSpec s;
            long type, fmt, order, interval, period;
            if (!f[3].ToLong(&type) || !f[4].ToLong(&fmt) || !f[5].ToLong(&order) || !f[6].ToCDouble(&s.min) ||
                !f[7].ToCDouble(&s.max) || !f[8].ToCDouble(&s.step) || !f[9].ToLong(&interval) ||
                !f[10].ToLong(&period) || type <= 0 || type >= SimSpec::Count || fmt < 0 ||
                fmt >= (long)DataFmt::Count || order < 0 || order >= (long)WordOrder::Count) {
                ++bad;
                continue;
            }
            s.type = (SimSpec::Type)type;
            s.fmt = (DataFmt)fmt;
            s.order = (WordOrder)order;
            s.intervalMs = (int)interval;
            s.periodMs = (int)std::max(100L, period);
            s.value = s.type == SimSpec::Decrement ? s.max : s.min;
            m_sims[t][addr] = s;
        } else {
            ++bad;
            continue;
        }
        ++lines;
    }
    m_data.mutex.Unlock();
    m_log->Add(bad ? LogView::Error : LogView::Info,
               wxString::Format("Loaded %s: %d entries, %d invalid lines", dlg.GetPath(), lines, bad));
    for (wxGrid* g : m_grids)
        g->ForceRefresh();
}
