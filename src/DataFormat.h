// Register value formatting / parsing (16/32/64-bit integers, floats, ASCII)
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <wx/arrstr.h>
#include <wx/string.h>

enum class DataFmt {
    Unsigned16 = 0,
    Signed16,
    Hex16,
    Binary16,
    Unsigned32,
    Signed32,
    Float32,
    Unsigned64,
    Signed64,
    Float64,
    Ascii,
    Count
};

// Byte order of a multi register value.  Letters are bytes from MSB (A) to LSB.
enum class WordOrder {
    ABCD = 0, // big endian (Modbus standard)
    CDAB,     // word swapped
    BADC,     // byte swapped
    DCBA,     // little endian
    Count
};

namespace DataFormat {

wxArrayString FormatNames();
wxArrayString OrderNames();

// Number of 16-bit registers occupied by one value of the format.
int RegisterCount(DataFmt fmt);

// Format the value starting at regs[0].  'regs' must contain at least
// RegisterCount(fmt) entries (missing words are treated as 0).
wxString Format(const uint16_t* regs, int available, DataFmt fmt, WordOrder order);

// Parse one value into registers.  Returns false on syntax error.
bool Parse(const wxString& text, DataFmt fmt, WordOrder order, std::vector<uint16_t>& out);

// Convert a numeric value into registers (integers are rounded / clamped).
void FromDouble(double value, DataFmt fmt, WordOrder order, std::vector<uint16_t>& out);

// Parse a list of values separated by ',', ';', or whitespace.
bool ParseList(const wxString& text, DataFmt fmt, WordOrder order,
               std::vector<uint16_t>& out, wxString* error = nullptr);

// Parse a list of bit values (0/1, true/false, on/off).
bool ParseBits(const wxString& text, std::vector<uint8_t>& out, wxString* error = nullptr);

// Parse an integer accepting decimal, 0x hex, 0b binary.
bool ParseInt(const wxString& text, long long& value);

wxString HexDump(const uint8_t* data, int len);
bool ParseHexBytes(const wxString& text, std::vector<uint8_t>& out);

wxString ToBinary16(uint16_t v);

} // namespace DataFormat
