#include "ConnectionPanel.h"

#include "UiUtil.h"

ConnectionPanel::ConnectionPanel(wxWindow* parent, bool server)
    : wxPanel(parent), m_server(server)
{
    auto* top = new wxBoxSizer(wxHORIZONTAL);

    wxArrayString types;
    types.Add(server ? "Modbus TCP Server" : "Modbus TCP");
    types.Add(server ? "Modbus RTU Slave" : "Modbus RTU");
    m_type = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, types);
    m_type->SetSelection(0);
    top->Add(m_type, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    // TCP
    m_tcpPanel = new wxPanel(this);
    {
        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->Add(new wxStaticText(m_tcpPanel, wxID_ANY, server ? "Listen IP:" : "Host:"), 0,
               wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_host = new wxTextCtrl(m_tcpPanel, wxID_ANY, server ? "0.0.0.0" : "127.0.0.1",
                                wxDefaultPosition, wxSize(130, -1));
        s->Add(m_host, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        s->Add(new wxStaticText(m_tcpPanel, wxID_ANY, "Port:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_port = new wxSpinCtrl(m_tcpPanel, wxID_ANY, "502", wxDefaultPosition, SpinSize(m_tcpPanel, 90),
                                wxSP_ARROW_KEYS, 1, 65535, 502);
        s->Add(m_port, 0, wxALIGN_CENTER_VERTICAL);
        m_tcpPanel->SetSizer(s);
    }
    top->Add(m_tcpPanel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    // RTU
    m_rtuPanel = new wxPanel(this);
    {
        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->Add(new wxStaticText(m_rtuPanel, wxID_ANY, "Port:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_serial = new wxComboBox(m_rtuPanel, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxSize(140, -1), ModbusUtil::EnumerateSerialPorts());
        if (m_serial->GetCount())
            m_serial->SetSelection(0);
        s->Add(m_serial, 0, wxALIGN_CENTER_VERTICAL);
        auto* refresh = new wxButton(m_rtuPanel, wxID_ANY, "R", wxDefaultPosition, wxSize(28, -1));
        refresh->SetToolTip("Refresh serial port list");
        refresh->Bind(wxEVT_BUTTON, &ConnectionPanel::OnRefreshPorts, this);
        s->Add(refresh, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        wxArrayString bauds;
        for (const char* b : {"1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200",
                              "230400", "460800", "921600"})
            bauds.Add(b);
        m_baud = new wxComboBox(m_rtuPanel, wxID_ANY, "9600", wxDefaultPosition, wxSize(90, -1), bauds);
        s->Add(m_baud, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

        wxArrayString dbits;
        dbits.Add("7");
        dbits.Add("8");
        m_dataBits = new wxChoice(m_rtuPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, dbits);
        m_dataBits->SetSelection(1);
        s->Add(m_dataBits, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);

        wxArrayString par;
        par.Add("None");
        par.Add("Even");
        par.Add("Odd");
        m_parity = new wxChoice(m_rtuPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, par);
        m_parity->SetSelection(0);
        s->Add(m_parity, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);

        wxArrayString sbits;
        sbits.Add("1");
        sbits.Add("2");
        m_stopBits = new wxChoice(m_rtuPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, sbits);
        m_stopBits->SetSelection(0);
        s->Add(m_stopBits, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

        m_rs485 = new wxCheckBox(m_rtuPanel, wxID_ANY, "RS485");
        m_rs485->SetToolTip("Enable Linux RS485 mode (TIOCSRS485). Ignored on Windows.");
        s->Add(m_rs485, 0, wxALIGN_CENTER_VERTICAL);
        m_rtuPanel->SetSizer(s);
    }
    top->Add(m_rtuPanel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    top->Add(new wxStaticText(this, wxID_ANY, server ? "Byte TO:" : "Timeout:"), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_respTimeout = new wxSpinCtrl(this, wxID_ANY, "1000", wxDefaultPosition, SpinSize(this, 80),
                                   wxSP_ARROW_KEYS, 10, 60000, 1000);
    m_respTimeout->SetToolTip("Response timeout [ms]");
    m_byteTimeout = new wxSpinCtrl(this, wxID_ANY, "500", wxDefaultPosition, SpinSize(this, 80),
                                   wxSP_ARROW_KEYS, 0, 10000, 500);
    m_byteTimeout->SetToolTip("Byte (inter character) timeout [ms]");
    if (server) {
        // Response timeout has no meaning for a server
        m_respTimeout->Hide();
    } else {
        top->Add(m_respTimeout, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        top->Add(new wxStaticText(this, wxID_ANY, "Byte:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    }
    top->Add(m_byteTimeout, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    m_debug = new wxCheckBox(this, wxID_ANY, "Debug");
    m_debug->SetToolTip("Enable libmodbus debug output on stdout");
    top->Add(m_debug, 0, wxALIGN_CENTER_VERTICAL);

    SetSizer(top);
    m_type->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateVisibility(); });
    UpdateVisibility();
}

void ConnectionPanel::UpdateVisibility()
{
    bool tcp = m_type->GetSelection() == 0;
    m_tcpPanel->Show(tcp);
    m_rtuPanel->Show(!tcp);
    Layout();
    if (GetParent())
        GetParent()->Layout();
}

void ConnectionPanel::OnRefreshPorts(wxCommandEvent&)
{
    wxString cur = m_serial->GetValue();
    m_serial->Set(ModbusUtil::EnumerateSerialPorts());
    m_serial->SetValue(cur.empty() && m_serial->GetCount() ? m_serial->GetString(0) : cur);
}

ConnSettings ConnectionPanel::GetSettings() const
{
    ConnSettings s;
    s.type = m_type->GetSelection() == 0 ? ConnSettings::TCP : ConnSettings::RTU;
    s.host = m_host->GetValue().Trim().Trim(false);
    s.port = m_port->GetValue();
    s.serialPort = m_serial->GetValue().Trim().Trim(false);
    long baud = 9600;
    m_baud->GetValue().ToLong(&baud);
    s.baud = (int)baud;
    static const char parities[] = {'N', 'E', 'O'};
    s.parity = parities[m_parity->GetSelection() < 0 ? 0 : m_parity->GetSelection()];
    s.dataBits = m_dataBits->GetSelection() == 0 ? 7 : 8;
    s.stopBits = m_stopBits->GetSelection() == 1 ? 2 : 1;
    s.rs485 = m_rs485->GetValue();
    s.responseTimeoutMs = m_respTimeout->GetValue();
    s.byteTimeoutMs = m_byteTimeout->GetValue();
    s.debug = m_debug->GetValue();
    return s;
}

void ConnectionPanel::SetSettings(const ConnSettings& s)
{
    m_type->SetSelection(s.type == ConnSettings::TCP ? 0 : 1);
    m_host->SetValue(s.host);
    m_port->SetValue(s.port);
    m_serial->SetValue(s.serialPort);
    m_baud->SetValue(wxString::Format("%d", s.baud));
    m_parity->SetSelection(s.parity == 'E' ? 1 : s.parity == 'O' ? 2 : 0);
    m_dataBits->SetSelection(s.dataBits == 7 ? 0 : 1);
    m_stopBits->SetSelection(s.stopBits == 2 ? 1 : 0);
    m_rs485->SetValue(s.rs485);
    m_respTimeout->SetValue(s.responseTimeoutMs);
    m_byteTimeout->SetValue(s.byteTimeoutMs);
    m_debug->SetValue(s.debug);
    UpdateVisibility();
}

void ConnectionPanel::EnableEditing(bool enable)
{
    for (wxWindow* w : GetChildren())
        w->Enable(enable);
}
