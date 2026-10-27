// winsock2.h must come before windows.h (pulled in by wx)
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "SlaveServer.h"

#include <cerrno>
#include <map>
#include <wx/time.h>

wxDEFINE_EVENT(EVT_SLAVE_STATUS, wxThreadEvent);
wxDEFINE_EVENT(EVT_SLAVE_REQUEST, wxThreadEvent);

static void CloseSocket(int s)
{
#ifdef _WIN32
    closesocket((SOCKET)s);
#else
    close(s);
#endif
}

// ---------------------------------------------------------------------------
SlaveData::SlaveData()
{
    map = modbus_mapping_new(65536, 65536, 65536, 65536);
    for (auto& v : changedAt)
        v.assign(65536, 0);
}

SlaveData::~SlaveData()
{
    modbus_mapping_free(map);
}

void SlaveData::MarkChanged(int table, int addr, int count)
{
    long now = (long)wxGetLocalTimeMillis().GetLo();
    for (int i = 0; i < count && addr + i < 65536; ++i)
        changedAt[table][addr + i] = now ? now : 1;
}

uint16_t SlaveData::GetReg(int table, int addr) const
{
    return table == TBL_HOLDING ? map->tab_registers[addr] : map->tab_input_registers[addr];
}

void SlaveData::SetReg(int table, int addr, uint16_t v)
{
    if (table == TBL_HOLDING)
        map->tab_registers[addr] = v;
    else
        map->tab_input_registers[addr] = v;
}

uint8_t SlaveData::GetBit(int table, int addr) const
{
    return table == TBL_COILS ? map->tab_bits[addr] : map->tab_input_bits[addr];
}

void SlaveData::SetBit(int table, int addr, uint8_t v)
{
    if (table == TBL_COILS)
        map->tab_bits[addr] = v ? 1 : 0;
    else
        map->tab_input_bits[addr] = v ? 1 : 0;
}

// ---------------------------------------------------------------------------
SlaveServer::SlaveServer(wxEvtHandler* sink, const ConnSettings& s, SlaveData* data, SlaveOptions* opts)
    : wxThread(wxTHREAD_JOINABLE), m_sink(sink), m_settings(s), m_data(data), m_opts(opts)
{
}

SlaveServer::~SlaveServer()
{
    if (m_ctx)
        modbus_free(m_ctx);
}

void SlaveServer::SendStatus(int code, const wxString& msg, long clients)
{
    auto* ev = new wxThreadEvent(EVT_SLAVE_STATUS);
    ev->SetInt(code);
    ev->SetString(msg.Clone());
    ev->SetExtraLong(clients);
    wxQueueEvent(m_sink, ev);
}

wxThread::ExitCode SlaveServer::Entry()
{
    wxString err;
    m_ctx = ModbusUtil::CreateContext(m_settings, true, err);
    if (!m_ctx) {
        SendStatus(0, "Create context failed: " + err);
        SendStatus(-1, wxEmptyString);
        return (ExitCode)1;
    }
    if (m_settings.type == ConnSettings::TCP)
        RunTcp();
    else
        RunRtu();
    SendStatus(-1, "Stopped");
    return (ExitCode)0;
}

void SlaveServer::RunTcp()
{
    int server = modbus_tcp_listen(m_ctx, 16);
    if (server == -1) {
        SendStatus(0, wxString::Format("Listen on %s failed: %s", m_settings.Describe(),
                                       ModbusUtil::ErrorString(errno)));
        return;
    }
    SendStatus(1, "Listening on " + m_settings.Describe(), 0);

    std::map<int, wxString> clients; // socket -> peer
    uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];

    while (!m_stop) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server, &rfds);
        int maxfd = server;
        for (auto& c : clients) {
            FD_SET(c.first, &rfds);
            if (c.first > maxfd)
                maxfd = c.first;
        }
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        int rc = select(maxfd + 1, &rfds, nullptr, nullptr, &tv);
        if (rc < 0) {
#ifndef _WIN32
            if (errno == EINTR)
                continue;
#endif
            SendStatus(0, "select() failed");
            break;
        }
        if (rc == 0)
            continue;

        if (FD_ISSET(server, &rfds)) {
            sockaddr_in addr;
            socklen_t len = sizeof(addr);
            int s = (int)accept(server, (sockaddr*)&addr, &len);
            if (s >= 0) {
                char ip[64] = "?";
#ifdef _WIN32
                const char* p = inet_ntoa(addr.sin_addr);
                if (p)
                    snprintf(ip, sizeof(ip), "%s", p);
#else
                inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
#endif
                wxString peer = wxString::Format("%s:%d", ip, ntohs(addr.sin_port));
                clients[s] = peer;
                SendStatus(2, "Client connected: " + peer, (long)clients.size());
            }
        }

        for (auto it = clients.begin(); it != clients.end();) {
            int s = it->first;
            if (!FD_ISSET(s, &rfds)) {
                ++it;
                continue;
            }
            modbus_set_socket(m_ctx, s);
            int n = modbus_receive(m_ctx, query);
            if (n > 0) {
                Handle(query, n, it->second);
                ++it;
            } else if (n == 0) {
                ++it;
            } else {
                wxString peer = it->second;
                CloseSocket(s);
                it = clients.erase(it);
                SendStatus(2, wxString::Format("Client disconnected: %s (%s)", peer, ModbusUtil::ErrorString(errno)),
                           (long)clients.size());
            }
        }
    }

    for (auto& c : clients)
        CloseSocket(c.first);
    CloseSocket(server);
    modbus_set_socket(m_ctx, -1);
}

void SlaveServer::RunRtu()
{
    int unit = m_opts->unitId;
    modbus_set_slave(m_ctx, unit);
    if (modbus_connect(m_ctx) == -1) {
        SendStatus(0, wxString::Format("Open %s failed: %s", m_settings.Describe(), ModbusUtil::ErrorString(errno)));
        return;
    }
#if !defined(_WIN32)
    if (m_settings.rs485 && modbus_rtu_set_serial_mode(m_ctx, MODBUS_RTU_RS485) == -1)
        SendStatus(2, "RS485 mode not supported: " + ModbusUtil::ErrorString(errno));
#endif
    // wake up regularly to check the stop flag
    modbus_set_indication_timeout(m_ctx, 0, 200000);
    // After a request for another unit libmodbus swallows the next frame as that
    // unit's reply, waiting up to the response timeout.  Keep the window short so
    // that a request following an unanswered one is not lost.
    modbus_set_response_timeout(m_ctx, 0, 200000);
    SendStatus(1, wxString::Format("Opened %s (unit %d)", m_settings.Describe(), unit));

    uint8_t query[MODBUS_RTU_MAX_ADU_LENGTH];
    wxString peer = m_settings.serialPort;
    while (!m_stop) {
        if (m_opts->unitId != unit) {
            unit = m_opts->unitId;
            modbus_set_slave(m_ctx, unit);
        }
        int n = modbus_receive(m_ctx, query);
        if (n > 0) {
            Handle(query, n, peer);
        } else if (n == 0) {
            // request for another slave on the bus
            auto info = std::make_shared<SlaveRequestInfo>();
            info->peer = peer;
            info->result = SlaveRequestInfo::Ignored;
            info->unit = query[0];
            info->fc = query[1];
            info->error = "other unit";
            auto* ev = new wxThreadEvent(EVT_SLAVE_REQUEST);
            ev->SetPayload(info);
            wxQueueEvent(m_sink, ev);
        } else {
            int e = errno;
            if (e == ETIMEDOUT)
                continue;
            auto info = std::make_shared<SlaveRequestInfo>();
            info->peer = peer;
            info->result = SlaveRequestInfo::Failed;
            info->error = ModbusUtil::ErrorString(e);
            auto* ev = new wxThreadEvent(EVT_SLAVE_REQUEST);
            ev->SetPayload(info);
            wxQueueEvent(m_sink, ev);
            if (e != EMBBADCRC && e != EMBBADDATA && e != EMBBADSLAVE && e != EMBMDATA)
                wxMilliSleep(100); // serious port error: avoid busy loop
        }
    }
    modbus_close(m_ctx);
}

static int Get16(const uint8_t* p)
{
    return (p[0] << 8) | p[1];
}

void SlaveServer::Handle(const uint8_t* query, int len, const wxString& peer)
{
    int hl = modbus_get_header_length(m_ctx);
    auto info = std::make_shared<SlaveRequestInfo>();
    info->peer = peer;
    info->frame.assign(query, query + len);
    info->unit = query[hl - 1];
    info->fc = query[hl];
    const uint8_t* pdu = query + hl;
    int pduLen = len - hl - (m_settings.type == ConnSettings::RTU ? 2 : 0);

    switch (info->fc) {
    case 1: case 2: case 3: case 4: case 5: case 6: case 15: case 16:
        if (pduLen >= 5) {
            info->addr = Get16(pdu + 1);
            info->count = (info->fc == 5 || info->fc == 6) ? 1 : Get16(pdu + 3);
        }
        break;
    case 22:
        if (pduLen >= 3) {
            info->addr = Get16(pdu + 1);
            info->count = 1;
        }
        break;
    case 23:
        if (pduLen >= 9) {
            info->addr = Get16(pdu + 1);
            info->count = Get16(pdu + 3);
            info->writeAddr = Get16(pdu + 5);
            info->writeCount = Get16(pdu + 7);
        }
        break;
    default:
        break;
    }

    bool tcp = m_settings.type == ConnSettings::TCP;
    if (tcp && !m_opts->anyUnit && info->unit != m_opts->unitId) {
        info->result = SlaveRequestInfo::Ignored;
        info->error = "unit id mismatch";
    } else if (m_opts->noResponse) {
        info->result = SlaveRequestInfo::NoResponse;
    } else {
        int delay = m_opts->delayMs;
        if (delay > 0)
            wxMilliSleep(delay);
        int exc = m_opts->forceException;
        int rc;
        if (exc > 0) {
            rc = modbus_reply_exception(m_ctx, query, exc);
            info->result = SlaveRequestInfo::Exception;
            info->exception = exc;
        } else {
            wxMutexLocker lock(m_data->mutex);
            rc = modbus_reply(m_ctx, query, len, m_data->map);
            if (rc != -1) {
                switch (info->fc) {
                case 5: case 15:
                    m_data->MarkChanged(TBL_COILS, info->addr, info->count);
                    break;
                case 6: case 16: case 22:
                    m_data->MarkChanged(TBL_HOLDING, info->addr, info->count);
                    break;
                case 23:
                    m_data->MarkChanged(TBL_HOLDING, info->writeAddr, info->writeCount);
                    break;
                }
            }
            // modbus_reply answers illegal function / address with an exception
            if (info->fc > 6 && info->fc != 15 && info->fc != 16 && info->fc != 17 && info->fc != 22 &&
                info->fc != 23) {
                info->result = SlaveRequestInfo::Exception;
                info->exception = MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
            }
        }
        if (rc == -1 && info->unit != 0) {
            info->result = SlaveRequestInfo::Failed;
            info->error = ModbusUtil::ErrorString(errno);
        }
    }
    auto* ev = new wxThreadEvent(EVT_SLAVE_REQUEST);
    ev->SetPayload(info);
    wxQueueEvent(m_sink, ev);
}
