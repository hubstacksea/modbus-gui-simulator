#include "MasterWorker.h"

#include <cerrno>
#include <chrono>

wxDEFINE_EVENT(EVT_MASTER_STATUS, wxThreadEvent);
wxDEFINE_EVENT(EVT_MASTER_RESULT, wxThreadEvent);

using Clock = std::chrono::steady_clock;

MasterWorker::MasterWorker(wxEvtHandler* sink, const ConnSettings& settings)
    : wxThread(wxTHREAD_JOINABLE), m_sink(sink), m_settings(settings)
{
}

MasterWorker::~MasterWorker()
{
    if (m_ctx) {
        modbus_close(m_ctx);
        modbus_free(m_ctx);
    }
}

void MasterWorker::Post(const MasterRequest& req)
{
    m_queue.Post(req);
}

void MasterWorker::SetPoll(bool enabled, const MasterRequest& req, int intervalMs)
{
    {
        wxCriticalSectionLocker lock(m_pollLock);
        m_pollEnabled = enabled;
        m_pollReq = req;
        m_pollReq.poll = true;
        m_pollInterval = intervalMs < 10 ? 10 : intervalMs;
        m_pollChanged = true;
    }
    // wake the thread up so that the new poll starts immediately
    MasterRequest wake;
    wake.kind = MasterRequest::Quit;
    wake.unit = -1; // marker: wake only
    m_queue.Post(wake);
}

void MasterWorker::RequestStop()
{
    m_stop = true;
    m_cancelScan = true;
    MasterRequest q;
    q.kind = MasterRequest::Quit;
    m_queue.Post(q);
}

void MasterWorker::SendStatus(int connected, const wxString& msg)
{
    auto* ev = new wxThreadEvent(EVT_MASTER_STATUS);
    ev->SetInt(connected);
    ev->SetString(msg.Clone());
    wxQueueEvent(m_sink, ev);
}

void MasterWorker::SendResult(const MasterResultPtr& r)
{
    auto* ev = new wxThreadEvent(EVT_MASTER_RESULT);
    ev->SetPayload(r);
    wxQueueEvent(m_sink, ev);
}

bool MasterWorker::IsConnectionError(int err) const
{
    if (m_settings.type != ConnSettings::TCP)
        return false;
    // Every non Modbus error other than a timeout means the socket is unusable.
    return err != ETIMEDOUT && err < MODBUS_ENOBASE;
}

bool MasterWorker::Connect()
{
    if (!m_ctx) {
        wxString err;
        m_ctx = ModbusUtil::CreateContext(m_settings, false, err);
        if (!m_ctx) {
            SendStatus(0, "Create context failed: " + err);
            return false;
        }
    }
    if (modbus_connect(m_ctx) == -1) {
        SendStatus(0, wxString::Format("Connect failed (%s): %s", m_settings.Describe(),
                                       ModbusUtil::ErrorString(errno)));
        return false;
    }
#if !defined(_WIN32)
    if (m_settings.type == ConnSettings::RTU && m_settings.rs485) {
        if (modbus_rtu_set_serial_mode(m_ctx, MODBUS_RTU_RS485) == -1)
            SendStatus(1, "RS485 mode not supported: " + ModbusUtil::ErrorString(errno));
    }
#endif
    m_connected = true;
    SendStatus(1, "Connected: " + m_settings.Describe());
    return true;
}

wxThread::ExitCode MasterWorker::Entry()
{
    if (!Connect()) {
        SendStatus(-1, wxEmptyString); // thread finished
        return (ExitCode)1;
    }

    Clock::time_point nextPoll = Clock::now();
    while (!m_stop) {
        bool pollEnabled;
        MasterRequest pollReq;
        int interval;
        {
            wxCriticalSectionLocker lock(m_pollLock);
            pollEnabled = m_pollEnabled;
            pollReq = m_pollReq;
            interval = m_pollInterval;
            if (m_pollChanged) {
                nextPoll = Clock::now();
                m_pollChanged = false;
            }
        }

        long waitMs = 200;
        if (pollEnabled) {
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(nextPoll - Clock::now()).count();
            waitMs = diff < 0 ? 0 : (long)std::min<long long>(diff, 200);
        }

        MasterRequest req;
        wxMessageQueueError qe = m_queue.ReceiveTimeout(waitMs, req);
        if (m_stop)
            break;
        if (qe == wxMSGQUEUE_NO_ERROR) {
            if (req.kind == MasterRequest::Quit)
                continue;
            if (req.kind == MasterRequest::Scan) {
                DoScan(req);
                continue;
            }
            SendResult(Execute(req));
            continue;
        }
        if (pollEnabled && Clock::now() >= nextPoll) {
            SendResult(Execute(pollReq));
            nextPoll += std::chrono::milliseconds(interval);
            if (nextPoll < Clock::now())
                nextPoll = Clock::now() + std::chrono::milliseconds(interval);
        }
    }

    if (m_ctx)
        modbus_close(m_ctx);
    m_connected = false;
    SendStatus(-1, "Disconnected");
    return (ExitCode)0;
}

MasterResultPtr MasterWorker::Execute(const MasterRequest& req)
{
    auto r = std::make_shared<MasterResult>();
    r->req = req;

    if (!m_connected) {
        // TCP: try to reconnect transparently
        modbus_close(m_ctx);
        if (modbus_connect(m_ctx) == -1) {
            r->err = errno;
            r->error = "Not connected: " + ModbusUtil::ErrorString(errno);
            return r;
        }
        m_connected = true;
        SendStatus(1, "Reconnected: " + m_settings.Describe());
    }

    modbus_set_slave(m_ctx, req.unit);
    auto t0 = Clock::now();
    int rc = -1;

    switch (req.kind) {
    case MasterRequest::Read:
        if (req.fc == 1 || req.fc == 2) {
            r->bits.resize(req.count);
            rc = req.fc == 1 ? modbus_read_bits(m_ctx, req.addr, req.count, r->bits.data())
                             : modbus_read_input_bits(m_ctx, req.addr, req.count, r->bits.data());
        } else {
            r->regs.resize(req.count);
            rc = req.fc == 3 ? modbus_read_registers(m_ctx, req.addr, req.count, r->regs.data())
                             : modbus_read_input_registers(m_ctx, req.addr, req.count, r->regs.data());
        }
        break;
    case MasterRequest::WriteCoil:
        rc = modbus_write_bit(m_ctx, req.addr, req.bits.empty() ? 0 : req.bits[0]);
        break;
    case MasterRequest::WriteRegister:
        rc = modbus_write_register(m_ctx, req.addr, req.regs.empty() ? 0 : req.regs[0]);
        break;
    case MasterRequest::WriteCoils:
        rc = modbus_write_bits(m_ctx, req.addr, (int)req.bits.size(), req.bits.data());
        break;
    case MasterRequest::WriteRegisters:
        rc = modbus_write_registers(m_ctx, req.addr, (int)req.regs.size(), req.regs.data());
        break;
    case MasterRequest::MaskWrite:
        rc = modbus_mask_write_register(m_ctx, req.addr, req.andMask, req.orMask);
        break;
    case MasterRequest::ReadWrite:
        r->regs.resize(req.count);
        rc = modbus_write_and_read_registers(m_ctx, req.writeAddr, (int)req.regs.size(),
                                             req.regs.data(), req.addr, req.count, r->regs.data());
        break;
    case MasterRequest::ReportSlaveId: {
        r->raw.resize(MODBUS_MAX_PDU_LENGTH);
        rc = modbus_report_slave_id(m_ctx, MODBUS_MAX_PDU_LENGTH, r->raw.data());
        if (rc >= 0)
            r->raw.resize(rc);
        break;
    }
    case MasterRequest::Raw: {
        rc = modbus_send_raw_request(m_ctx, req.raw.data(), (int)req.raw.size());
        if (rc != -1) {
            r->raw.resize(MODBUS_MAX_ADU_LENGTH);
            rc = modbus_receive_confirmation(m_ctx, r->raw.data());
            if (rc >= 0)
                r->raw.resize(rc);
        }
        break;
    }
    default:
        break;
    }

    r->ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (rc == -1) {
        r->err = errno;
        r->error = ModbusUtil::ErrorString(errno);
        if (IsConnectionError(r->err)) {
            m_connected = false;
            modbus_close(m_ctx);
            SendStatus(2, "Connection lost: " + r->error);
        } else if (m_settings.type == ConnSettings::RTU && r->err == ETIMEDOUT) {
            modbus_flush(m_ctx);
        }
    } else {
        r->ok = true;
    }
    return r;
}

void MasterWorker::DoScan(const MasterRequest& req)
{
    m_cancelScan = false;
    std::vector<int> found;
    for (int id = req.scanFrom; id <= req.scanTo && !m_cancelScan && !m_stop; ++id) {
        MasterRequest probe = req;
        probe.kind = MasterRequest::Read;
        probe.unit = id;
        probe.count = 1;
        MasterResultPtr pr = Execute(probe);
        // Any answer (data or a Modbus exception) means a device is present.
        bool present = pr->ok || (pr->err > MODBUS_ENOBASE && pr->err < EMBXGPATH + 1);
        if (present)
            found.push_back(id);
        auto prog = std::make_shared<MasterResult>();
        prog->req = req;
        prog->scanProgress = true;
        prog->scanCurrent = id;
        prog->ok = present;
        prog->error = pr->ok ? wxString("OK") : pr->error;
        prog->ms = pr->ms;
        SendResult(prog);
    }
    auto fin = std::make_shared<MasterResult>();
    fin->req = req;
    fin->ok = true;
    fin->scanFound = found;
    SendResult(fin);
}
