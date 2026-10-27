// Panel for editing ConnSettings (TCP / RTU parameters).
#pragma once

#include "ModbusUtil.h"
#include <wx/wx.h>

class wxSpinCtrl;

class ConnectionPanel : public wxPanel {
public:
    ConnectionPanel(wxWindow* parent, bool server);

    ConnSettings GetSettings() const;
    void SetSettings(const ConnSettings& s);
    void EnableEditing(bool enable);

private:
    void UpdateVisibility();
    void OnRefreshPorts(wxCommandEvent&);

    bool m_server;
    wxChoice* m_type;
    wxPanel* m_tcpPanel;
    wxPanel* m_rtuPanel;
    wxTextCtrl* m_host;
    wxSpinCtrl* m_port;
    wxComboBox* m_serial;
    wxComboBox* m_baud;
    wxChoice* m_parity;
    wxChoice* m_dataBits;
    wxChoice* m_stopBits;
    wxCheckBox* m_rs485;
    wxSpinCtrl* m_respTimeout;
    wxSpinCtrl* m_byteTimeout;
    wxCheckBox* m_debug;
};
