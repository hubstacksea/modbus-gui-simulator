// Background thread that owns a libmodbus master context and executes requests.
#pragma once

#include "ModbusUtil.h"

#include <atomic>
#include <memory>
#include <vector>
#include <wx/event.h>
#include <wx/msgqueue.h>
#include <wx/thread.h>

struct MasterRequest {
    enum Kind {
        Read,          // fc 1..4
        WriteCoil,     // fc 5
        WriteRegister, // fc 6
        WriteCoils,    // fc 15
        WriteRegisters,// fc 16
        MaskWrite,     // fc 22
        ReadWrite,     // fc 23
        ReportSlaveId, // fc 17
        Raw,           // arbitrary PDU
        Scan,          // unit id scan
        Quit           // internal
    };
    Kind kind = Read;
    int unit = 1;
    int fc = 3;
    int addr = 0;
    int count = 1;
    std::vector<uint16_t> regs;
    std::vector<uint8_t> bits;
    uint16_t andMask = 0xFFFF, orMask = 0;
    int writeAddr = 0;          // fc 23
    std::vector<uint8_t> raw;   // Raw: [unit][fc][data...]
    int scanFrom = 1, scanTo = 247;
    bool poll = false;
};

struct MasterResult {
    MasterRequest req;
    bool ok = false;
    int err = 0;
    wxString error;
    std::vector<uint16_t> regs;
    std::vector<uint8_t> bits;
    std::vector<uint8_t> raw; // response frame (raw / report slave id)
    double ms = 0;
    // Scan
    bool scanProgress = false;
    int scanCurrent = 0;
    std::vector<int> scanFound;
};

using MasterResultPtr = std::shared_ptr<MasterResult>;

// Status event: GetInt() = 1 connected, 0 disconnected; GetString() message.
wxDECLARE_EVENT(EVT_MASTER_STATUS, wxThreadEvent);
// Result event: payload MasterResultPtr.
wxDECLARE_EVENT(EVT_MASTER_RESULT, wxThreadEvent);

class MasterWorker : public wxThread {
public:
    MasterWorker(wxEvtHandler* sink, const ConnSettings& settings);
    ~MasterWorker() override;

    void Post(const MasterRequest& req);
    void SetPoll(bool enabled, const MasterRequest& req = MasterRequest(), int intervalMs = 1000);
    void RequestStop();
    void CancelScan() { m_cancelScan = true; }

protected:
    ExitCode Entry() override;

private:
    bool Connect();
    MasterResultPtr Execute(const MasterRequest& req);
    void DoScan(const MasterRequest& req);
    void SendStatus(int connected, const wxString& msg);
    void SendResult(const MasterResultPtr& r);
    bool IsConnectionError(int err) const;

    wxEvtHandler* m_sink;
    ConnSettings m_settings;
    modbus_t* m_ctx = nullptr;
    bool m_connected = false;
    wxMessageQueue<MasterRequest> m_queue;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_cancelScan{false};

    wxCriticalSection m_pollLock;
    bool m_pollEnabled = false;
    MasterRequest m_pollReq;
    int m_pollInterval = 1000;
    bool m_pollChanged = false;
};
