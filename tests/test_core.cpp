// Headless integration test: SlaveServer <-> MasterWorker over TCP, plus DataFormat.
#include "DataFormat.h"
#include "MasterWorker.h"
#include "SlaveServer.h"

#include <cstdio>
#include <cstdlib>
#include <wx/app.h>
#include <wx/evtloop.h>
#include <wx/init.h>

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
            ++g_fail;                                                    \
        }                                                                \
    } while (0)

class Sink : public wxEvtHandler {
public:
    std::vector<MasterResultPtr> results;
    std::vector<SlaveRequestInfoPtr> requests;
    int masterStatus = -99;
    int slaveStatus = -99;
    Sink()
    {
        Bind(EVT_MASTER_RESULT, [this](wxThreadEvent& e) { results.push_back(e.GetPayload<MasterResultPtr>()); });
        Bind(EVT_MASTER_STATUS, [this](wxThreadEvent& e) {
            masterStatus = e.GetInt();
            std::printf("  master status %d %s\n", e.GetInt(), (const char*)e.GetString().utf8_str());
        });
        Bind(EVT_SLAVE_STATUS, [this](wxThreadEvent& e) {
            slaveStatus = e.GetInt();
            std::printf("  slave status %d %s\n", e.GetInt(), (const char*)e.GetString().utf8_str());
        });
        Bind(EVT_SLAVE_REQUEST, [this](wxThreadEvent& e) { requests.push_back(e.GetPayload<SlaveRequestInfoPtr>()); });
    }
};

static void Pump(int ms, const std::function<bool()>& done = nullptr)
{
    wxStopWatch sw;
    while (sw.Time() < ms) {
        wxTheApp->ProcessPendingEvents();
        if (done && done())
            return;
        wxMilliSleep(5);
    }
}

static MasterResultPtr Run(Sink& sink, MasterWorker& w, const MasterRequest& r)
{
    size_t n = sink.results.size();
    w.Post(r);
    Pump(3000, [&] { return sink.results.size() > n; });
    if (sink.results.size() <= n) {
        std::printf("  (no result)\n");
        return nullptr;
    }
    auto res = sink.results.back();
    if (!res->ok)
        std::printf("  result error: %s (%.1f ms)\n", (const char*)res->error.utf8_str(), res->ms);
    return res;
}

static void TestFormats()
{
    std::vector<uint16_t> regs;
    CHECK(DataFormat::Parse("3.5", DataFmt::Float32, WordOrder::ABCD, regs));
    CHECK(regs.size() == 2 && regs[0] == 0x4060 && regs[1] == 0x0000);
    CHECK(DataFormat::Format(regs.data(), 2, DataFmt::Float32, WordOrder::ABCD) == "3.5");
    regs.clear();
    CHECK(DataFormat::Parse("0x12345678", DataFmt::Unsigned32, WordOrder::CDAB, regs));
    CHECK(regs[0] == 0x5678 && regs[1] == 0x1234);
    regs.clear();
    CHECK(DataFormat::Parse("0x12345678", DataFmt::Unsigned32, WordOrder::DCBA, regs));
    CHECK(regs[0] == 0x7856 && regs[1] == 0x3412);
    CHECK(DataFormat::Format(regs.data(), 2, DataFmt::Unsigned32, WordOrder::DCBA) == "305419896");
    regs.clear();
    CHECK(DataFormat::Parse("-2", DataFmt::Signed16, WordOrder::ABCD, regs) && regs[0] == 0xFFFE);
    regs.clear();
    CHECK(DataFormat::Parse("-1", DataFmt::Signed64, WordOrder::ABCD, regs) && regs.size() == 4 && regs[3] == 0xFFFF);
    CHECK(DataFormat::Format(regs.data(), 4, DataFmt::Signed64, WordOrder::ABCD) == "-1");
    regs.clear();
    CHECK(DataFormat::Parse("ff", DataFmt::Hex16, WordOrder::ABCD, regs) && regs[0] == 0xFF);
    CHECK(!DataFormat::Parse("70000", DataFmt::Unsigned16, WordOrder::ABCD, regs));
    CHECK(DataFormat::ParseList("AB", DataFmt::Ascii, WordOrder::ABCD, regs) && regs[0] == 0x4142);
    std::vector<uint8_t> bytes;
    CHECK(DataFormat::ParseHexBytes("01 03 00 00 00 0A", bytes) && bytes.size() == 6 && bytes[5] == 0x0A);
    regs.clear();
    DataFormat::FromDouble(1.25, DataFmt::Float64, WordOrder::ABCD, regs);
    CHECK(DataFormat::Format(regs.data(), 4, DataFmt::Float64, WordOrder::ABCD) == "1.25");
}

int main(int argc, char** argv)
{
    wxApp::SetInstance(new wxAppConsole);
    wxInitializer init(argc, argv);
    if (!init.IsOk())
        return 2;
    // No wxEventLoop: events are dispatched manually with ProcessPendingEvents().

    setvbuf(stdout, nullptr, _IONBF, 0);
    TestFormats();

    Sink sink;
    SlaveData data;
    SlaveOptions opts;
    data.map->tab_registers[10] = 1234;
    data.map->tab_input_registers[5] = 55;
    data.map->tab_input_bits[3] = 1;

    ConnSettings ss;
    ss.type = ConnSettings::TCP;
    ss.host = "127.0.0.1";
    ss.port = 15502;
    auto* server = new SlaveServer(&sink, ss, &data, &opts);
    server->Run();
    Pump(1000, [&] { return sink.slaveStatus == 1; });
    CHECK(sink.slaveStatus == 1);

    ConnSettings ms = ss;
    ms.responseTimeoutMs = 500;
    auto* worker = new MasterWorker(&sink, ms);
    worker->Run();
    Pump(2000, [&] { return sink.masterStatus == 1; });
    CHECK(sink.masterStatus == 1);

    MasterRequest r;
    r.kind = MasterRequest::Read;
    r.fc = 3;
    r.addr = 10;
    r.count = 2;
    auto res = Run(sink, *worker, r);
    CHECK(res && res->ok && res->regs[0] == 1234);

    r.fc = 4; r.addr = 5; r.count = 1;
    res = Run(sink, *worker, r);
    CHECK(res && res->ok && res->regs[0] == 55);

    r.fc = 2; r.addr = 0; r.count = 8;
    res = Run(sink, *worker, r);
    CHECK(res && res->ok && res->bits[3] == 1 && res->bits[2] == 0);

    MasterRequest w;
    w.kind = MasterRequest::WriteRegisters;
    w.fc = 16;
    w.addr = 100;
    DataFormat::Parse("3.5", DataFmt::Float32, WordOrder::ABCD, w.regs);
    res = Run(sink, *worker, w);
    CHECK(res && res->ok);
    CHECK(data.map->tab_registers[100] == 0x4060);

    w = MasterRequest();
    w.kind = MasterRequest::WriteCoils;
    w.fc = 15;
    w.addr = 7;
    w.bits = {1, 0, 1};
    res = Run(sink, *worker, w);
    CHECK(res && res->ok && data.map->tab_bits[7] == 1 && data.map->tab_bits[9] == 1);

    w = MasterRequest();
    w.kind = MasterRequest::WriteCoil;
    w.fc = 5; w.addr = 8; w.bits = {1};
    res = Run(sink, *worker, w);
    CHECK(res && res->ok && data.map->tab_bits[8] == 1);

    w = MasterRequest();
    w.kind = MasterRequest::WriteRegister;
    w.fc = 6; w.addr = 200; w.regs = {0x00F0};
    res = Run(sink, *worker, w);
    CHECK(res && res->ok && data.map->tab_registers[200] == 0xF0);

    w = MasterRequest();
    w.kind = MasterRequest::MaskWrite;
    w.fc = 22; w.addr = 200; w.andMask = 0xFF00; w.orMask = 0x0001; // (0xF0 & AND) | (OR & ~AND)
    res = Run(sink, *worker, w);
    CHECK(res && res->ok && data.map->tab_registers[200] == 0x0001);

    w = MasterRequest();
    w.kind = MasterRequest::ReadWrite;
    w.fc = 23; w.writeAddr = 300; w.regs = {7, 8}; w.addr = 300; w.count = 2;
    res = Run(sink, *worker, w);
    CHECK(res && res->ok && res->regs.size() == 2 && res->regs[1] == 8);

    w = MasterRequest();
    w.kind = MasterRequest::ReportSlaveId;
    w.fc = 17;
    res = Run(sink, *worker, w);
    CHECK(res && res->ok && res->raw.size() > 2);

    w = MasterRequest();
    w.kind = MasterRequest::Raw;
    DataFormat::ParseHexBytes("01 03 00 0A 00 01", w.raw);
    res = Run(sink, *worker, w);
    CHECK(res && res->ok && res->raw.size() == 11 && res->raw[9] == 0x04 && res->raw[10] == 0xD2);
    if (res)
        std::printf("  raw response: %s\n", (const char*)DataFormat::HexDump(res->raw.data(), (int)res->raw.size()).utf8_str());

    // forced exception
    opts.forceException = 2;
    res = Run(sink, *worker, r);
    CHECK(res && !res->ok && res->err == EMBXILADD);
    opts.forceException = 0;

    // unit id filter: no response -> timeout
    opts.anyUnit = false;
    opts.unitId = 5;
    r.unit = 1;
    res = Run(sink, *worker, r);
    CHECK(res && !res->ok && res->err == ETIMEDOUT);
    r.unit = 5;
    res = Run(sink, *worker, r);
    CHECK(res && res->ok);
    opts.anyUnit = true;

    // scan unit 4..6 with filter on unit 5
    opts.anyUnit = false;
    MasterRequest scan = r;
    scan.kind = MasterRequest::Scan;
    scan.scanFrom = 4;
    scan.scanTo = 6;
    size_t before = sink.results.size();
    worker->Post(scan);
    Pump(5000, [&] { return !sink.results.empty() && sink.results.size() > before &&
                            !sink.results.back()->scanProgress && sink.results.back()->req.kind == MasterRequest::Scan; });
    CHECK(!sink.results.empty() && sink.results.back()->scanFound.size() == 1 && sink.results.back()->scanFound[0] == 5);
    opts.anyUnit = true;

    // polling
    r.unit = 1;
    before = sink.results.size();
    worker->SetPoll(true, r, 100);
    Pump(1050);
    worker->SetPoll(false);
    size_t polls = sink.results.size() - before;
    std::printf("  polls in ~1s at 100ms: %zu\n", polls);
    CHECK(polls >= 8 && polls <= 13);

    // server restart -> master reconnects transparently
    server->RequestStop();
    server->Wait();
    delete server;
    res = Run(sink, *worker, r);
    CHECK(res && !res->ok);
    server = new SlaveServer(&sink, ss, &data, &opts);
    server->Run();
    Pump(500);
    res = Run(sink, *worker, r);
    if (res && !res->ok)
        res = Run(sink, *worker, r);
    CHECK(res && res->ok);

    worker->RequestStop();
    worker->Wait();
    delete worker;
    server->RequestStop();
    server->Wait();
    delete server;
    Pump(100);

    // ---- optional RTU test over a pty pair (see tests/pty_bridge.py) ----
    const char* rtuA = getenv("MODBUS_TEST_RTU_A");
    const char* rtuB = getenv("MODBUS_TEST_RTU_B");
    if (rtuA && rtuB) {
        std::printf("RTU test on %s <-> %s\n", rtuA, rtuB);
        ConnSettings rs;
        rs.type = ConnSettings::RTU;
        rs.serialPort = rtuB;
        rs.baud = 115200;
        rs.parity = 'E';
        opts.unitId = 17;
        sink.slaveStatus = sink.masterStatus = -99;
        server = new SlaveServer(&sink, rs, &data, &opts);
        server->Run();
        Pump(1000, [&] { return sink.slaveStatus == 1; });
        CHECK(sink.slaveStatus == 1);
        rs.serialPort = rtuA;
        rs.responseTimeoutMs = 300;
        worker = new MasterWorker(&sink, rs);
        worker->Run();
        Pump(1000, [&] { return sink.masterStatus == 1; });
        CHECK(sink.masterStatus == 1);
        r = MasterRequest();
        r.unit = 17;
        r.fc = 3;
        r.addr = 10;
        r.count = 1;
        res = Run(sink, *worker, r);
        CHECK(res && res->ok && res->regs[0] == 1234);
        w = MasterRequest();
        w.kind = MasterRequest::WriteRegisters;
        w.unit = 17;
        w.fc = 16;
        w.addr = 500;
        w.regs = {1, 2, 3};
        res = Run(sink, *worker, w);
        CHECK(res && res->ok && data.map->tab_registers[502] == 3);
        r.unit = 18; // other unit on the bus: must time out
        res = Run(sink, *worker, r);
        CHECK(res && !res->ok && res->err == ETIMEDOUT);
        r.unit = 17;
        res = Run(sink, *worker, r);
        CHECK(res && res->ok);
        worker->RequestStop();
        worker->Wait();
        delete worker;
        server->RequestStop();
        server->Wait();
        delete server;
        Pump(100);
    }

    std::printf("slave saw %zu requests\n", sink.requests.size());
    std::printf(g_fail ? "FAILED (%d)\n" : "ALL TESTS PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
