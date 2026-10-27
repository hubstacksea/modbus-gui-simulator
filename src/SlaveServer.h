// Modbus slave (server) thread and the shared register image.
#pragma once

#include "ModbusUtil.h"

#include <atomic>
#include <memory>
#include <vector>
#include <wx/event.h>
#include <wx/thread.h>

enum TableKind { TBL_COILS = 0, TBL_DISCRETE, TBL_HOLDING, TBL_INPUT, TBL_COUNT };

// Full 0..65535 address space for all four tables, shared between the GUI
// and the server thread.  Always lock 'mutex' while accessing 'map'.
class SlaveData {
public:
    SlaveData();
    ~SlaveData();

    modbus_mapping_t* map;
    wxMutex mutex;

    // Time stamp (ms, wxGetLocalTimeMillis low part) of the last write by a master.
    std::vector<long> changedAt[TBL_COUNT];
    void MarkChanged(int table, int addr, int count);

    uint16_t GetReg(int table, int addr) const;
    void SetReg(int table, int addr, uint16_t v);
    uint8_t GetBit(int table, int addr) const;
    void SetBit(int table, int addr, uint8_t v);
};

struct SlaveOptions {
    std::atomic<int> unitId{1};
    std::atomic<bool> anyUnit{true};  // TCP only
    std::atomic<int> delayMs{0};
    std::atomic<bool> noResponse{false};
    std::atomic<int> forceException{0}; // 0 = none
};

struct SlaveRequestInfo {
    wxString peer;
    std::vector<uint8_t> frame; // full ADU as received
    int unit = 0;
    int fc = 0;
    int addr = -1;
    int count = 0;
    int writeAddr = -1; // fc 23
    int writeCount = 0;
    enum Result { Replied, Exception, Ignored, NoResponse, Failed } result = Replied;
    int exception = 0;
    wxString error;
};
using SlaveRequestInfoPtr = std::shared_ptr<SlaveRequestInfo>;

// GetInt(): 1 running, 0 error, -1 stopped, 2 info. GetExtraLong(): client count
wxDECLARE_EVENT(EVT_SLAVE_STATUS, wxThreadEvent);
// payload: SlaveRequestInfoPtr
wxDECLARE_EVENT(EVT_SLAVE_REQUEST, wxThreadEvent);

class SlaveServer : public wxThread {
public:
    SlaveServer(wxEvtHandler* sink, const ConnSettings& s, SlaveData* data, SlaveOptions* opts);
    ~SlaveServer() override;

    void RequestStop() { m_stop = true; }

protected:
    ExitCode Entry() override;

private:
    void RunTcp();
    void RunRtu();
    void Handle(const uint8_t* query, int len, const wxString& peer);
    void SendStatus(int code, const wxString& msg, long clients = 0);

    wxEvtHandler* m_sink;
    ConnSettings m_settings;
    SlaveData* m_data;
    SlaveOptions* m_opts;
    modbus_t* m_ctx = nullptr;
    std::atomic<bool> m_stop{false};
};
