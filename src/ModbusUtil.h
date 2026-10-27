// Connection settings and libmodbus helpers shared by master and slave.
#pragma once

#include <modbus.h>
#include <wx/arrstr.h>
#include <wx/string.h>

struct ConnSettings {
    enum Type { TCP = 0, RTU = 1 };
    Type type = TCP;

    // TCP
    wxString host = "127.0.0.1"; // master: server address, slave: listen address
    int port = 502;

    // RTU
    wxString serialPort;
    int baud = 9600;
    char parity = 'N'; // 'N', 'E', 'O'
    int dataBits = 8;
    int stopBits = 1;
    bool rs485 = false; // Linux RS485 ioctl mode

    // Timeouts (ms)
    int responseTimeoutMs = 1000;
    int byteTimeoutMs = 500;

    bool debug = false; // libmodbus debug output to stdout/stderr

    wxString Describe() const;
};

namespace ModbusUtil {

// Creates (but does not connect) a libmodbus context. On failure returns
// nullptr and fills 'error'.
modbus_t* CreateContext(const ConnSettings& s, bool server, wxString& error);

// Applies timeouts / serial mode after context creation.
void ApplyOptions(modbus_t* ctx, const ConnSettings& s);

wxArrayString EnumerateSerialPorts();

// Converts a user friendly port name (COM10) to the device path libmodbus needs.
wxString SerialDevicePath(const wxString& name);

wxString ErrorString(int err);
wxString FunctionName(int fc);
wxString LibVersion();

} // namespace ModbusUtil
