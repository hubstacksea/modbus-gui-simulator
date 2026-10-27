#include "ModbusUtil.h"

#include <cerrno>
#include <wx/dir.h>
#include <wx/filename.h>
#ifdef __WXMSW__
#include <wx/msw/registry.h>
#endif

wxString ConnSettings::Describe() const
{
    if (type == TCP)
        return wxString::Format("TCP %s:%d", host, port);
    return wxString::Format("RTU %s %d %d%c%d", serialPort, baud, dataBits, parity, stopBits);
}

namespace ModbusUtil {

modbus_t* CreateContext(const ConnSettings& s, bool server, wxString& error)
{
    modbus_t* ctx = nullptr;
    if (s.type == ConnSettings::TCP) {
        wxString host = s.host;
        if (server && (host.empty() || host == "0.0.0.0" || host == "*"))
            ctx = modbus_new_tcp(nullptr, s.port);
        else
            ctx = modbus_new_tcp(host.utf8_str(), s.port);
    } else {
        if (s.serialPort.empty()) {
            error = "No serial port specified";
            return nullptr;
        }
        wxString dev = SerialDevicePath(s.serialPort);
        ctx = modbus_new_rtu(dev.utf8_str(), s.baud, s.parity, s.dataBits, s.stopBits);
    }
    if (!ctx) {
        error = ErrorString(errno);
        return nullptr;
    }
    ApplyOptions(ctx, s);
    return ctx;
}

void ApplyOptions(modbus_t* ctx, const ConnSettings& s)
{
    modbus_set_debug(ctx, s.debug ? TRUE : FALSE);
    modbus_set_response_timeout(ctx, s.responseTimeoutMs / 1000, (s.responseTimeoutMs % 1000) * 1000);
    modbus_set_byte_timeout(ctx, s.byteTimeoutMs / 1000, (s.byteTimeoutMs % 1000) * 1000);
    modbus_set_error_recovery(ctx, (modbus_error_recovery_mode)(MODBUS_ERROR_RECOVERY_PROTOCOL));
}

wxArrayString EnumerateSerialPorts()
{
    wxArrayString ports;
#ifdef __WXMSW__
    wxRegKey key(wxRegKey::HKLM, "HARDWARE\\DEVICEMAP\\SERIALCOMM");
    if (key.Exists() && key.Open(wxRegKey::Read)) {
        wxString name;
        long idx;
        bool cont = key.GetFirstValue(name, idx);
        while (cont) {
            wxString value;
            if (key.QueryValue(name, value))
                ports.Add(value);
            cont = key.GetNextValue(name, idx);
        }
    }
    ports.Sort([](const wxString& a, const wxString& b) {
        long na = 0, nb = 0;
        a.Mid(3).ToLong(&na);
        b.Mid(3).ToLong(&nb);
        return (int)(na - nb);
    });
#else
    static const char* patterns[] = {"ttyUSB*", "ttyACM*", "ttyS*", "ttyAMA*", "ttymxc*",
                                     "rfcomm*", "ttyXR*", "ttyCH*", "tnt*"};
    wxDir dir("/dev");
    if (dir.IsOpened()) {
        for (const char* p : patterns) {
            wxArrayString found;
            wxString f;
            bool cont = dir.GetFirst(&f, p, wxDIR_FILES | wxDIR_HIDDEN);
            while (cont) {
                found.Add("/dev/" + f);
                cont = dir.GetNext(&f);
            }
            found.Sort([](const wxString& a, const wxString& b) {
                if (a.length() != b.length())
                    return (int)a.length() - (int)b.length();
                return a.Cmp(b);
            });
            // Only list the first 4 ttyS* to avoid a huge list of phantom ports
            int limit = (wxString(p) == "ttyS*") ? 4 : 1000;
            for (size_t i = 0; i < found.size() && (int)i < limit; ++i)
                ports.Add(found[i]);
        }
    }
#endif
    return ports;
}

wxString SerialDevicePath(const wxString& name)
{
#ifdef __WXMSW__
    if (name.Upper().StartsWith("COM"))
        return "\\\\.\\" + name;
#endif
    return name;
}

wxString ErrorString(int err)
{
    return wxString::FromUTF8(modbus_strerror(err));
}

wxString FunctionName(int fc)
{
    switch (fc) {
    case 0x01: return "Read Coils";
    case 0x02: return "Read Discrete Inputs";
    case 0x03: return "Read Holding Registers";
    case 0x04: return "Read Input Registers";
    case 0x05: return "Write Single Coil";
    case 0x06: return "Write Single Register";
    case 0x07: return "Read Exception Status";
    case 0x08: return "Diagnostics";
    case 0x0F: return "Write Multiple Coils";
    case 0x10: return "Write Multiple Registers";
    case 0x11: return "Report Server ID";
    case 0x16: return "Mask Write Register";
    case 0x17: return "Read/Write Multiple Registers";
    case 0x2B: return "Encapsulated Interface";
    default:
        if (fc & 0x80)
            return wxString::Format("Exception (FC%02X)", fc & 0x7F);
        return wxString::Format("FC%02X", fc);
    }
}

wxString LibVersion()
{
    return wxString::Format("libmodbus %d.%d.%d (compiled %s)", libmodbus_version_major,
                            libmodbus_version_minor, libmodbus_version_micro,
                            LIBMODBUS_VERSION_STRING);
}

} // namespace ModbusUtil
