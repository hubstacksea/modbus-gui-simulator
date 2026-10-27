#include "DataFormat.h"

#include <cstring>
#include <cmath>
#include <wx/tokenzr.h>
#include <wx/wxcrt.h>

namespace DataFormat {

wxArrayString FormatNames()
{
    wxArrayString a;
    a.Add("Unsigned 16");
    a.Add("Signed 16");
    a.Add("Hex 16");
    a.Add("Binary 16");
    a.Add("Unsigned 32");
    a.Add("Signed 32");
    a.Add("Float 32");
    a.Add("Unsigned 64");
    a.Add("Signed 64");
    a.Add("Float 64");
    a.Add("ASCII");
    return a;
}

wxArrayString OrderNames()
{
    wxArrayString a;
    a.Add("ABCD (Big endian)");
    a.Add("CDAB (Word swap)");
    a.Add("BADC (Byte swap)");
    a.Add("DCBA (Little endian)");
    return a;
}

int RegisterCount(DataFmt fmt)
{
    switch (fmt) {
    case DataFmt::Unsigned32:
    case DataFmt::Signed32:
    case DataFmt::Float32:
        return 2;
    case DataFmt::Unsigned64:
    case DataFmt::Signed64:
    case DataFmt::Float64:
        return 4;
    default:
        return 1;
    }
}

// Convert registers (in wire order) to a big endian byte array honoring order.
static void RegsToBytes(const uint16_t* regs, int n, WordOrder order, uint8_t* bytes)
{
    bool wordSwap = (order == WordOrder::CDAB || order == WordOrder::DCBA);
    bool byteSwap = (order == WordOrder::BADC || order == WordOrder::DCBA);
    for (int i = 0; i < n; ++i) {
        uint16_t r = regs[wordSwap ? (n - 1 - i) : i];
        uint8_t hi = r >> 8, lo = r & 0xFF;
        if (byteSwap)
            std::swap(hi, lo);
        bytes[i * 2] = hi;
        bytes[i * 2 + 1] = lo;
    }
}

static void BytesToRegs(const uint8_t* bytes, int n, WordOrder order, uint16_t* regs)
{
    bool wordSwap = (order == WordOrder::CDAB || order == WordOrder::DCBA);
    bool byteSwap = (order == WordOrder::BADC || order == WordOrder::DCBA);
    for (int i = 0; i < n; ++i) {
        uint8_t hi = bytes[i * 2], lo = bytes[i * 2 + 1];
        if (byteSwap)
            std::swap(hi, lo);
        regs[wordSwap ? (n - 1 - i) : i] = (uint16_t)((hi << 8) | lo);
    }
}

static uint64_t BytesToU64(const uint8_t* b, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; ++i)
        v = (v << 8) | b[i];
    return v;
}

wxString ToBinary16(uint16_t v)
{
    wxString s;
    for (int i = 15; i >= 0; --i) {
        s += (v & (1u << i)) ? '1' : '0';
        if (i % 4 == 0 && i)
            s += ' ';
    }
    return s;
}

wxString Format(const uint16_t* regs, int available, DataFmt fmt, WordOrder order)
{
    int n = RegisterCount(fmt);
    uint16_t tmp[4] = {0, 0, 0, 0};
    for (int i = 0; i < n && i < available; ++i)
        tmp[i] = regs[i];

    switch (fmt) {
    case DataFmt::Unsigned16:
        return wxString::Format("%u", (unsigned)tmp[0]);
    case DataFmt::Signed16:
        return wxString::Format("%d", (int)(int16_t)tmp[0]);
    case DataFmt::Hex16:
        return wxString::Format("0x%04X", (unsigned)tmp[0]);
    case DataFmt::Binary16:
        return ToBinary16(tmp[0]);
    case DataFmt::Ascii: {
        uint8_t b[2];
        RegsToBytes(tmp, 1, order, b);
        wxString s;
        for (uint8_t c : b)
            s += (c >= 0x20 && c < 0x7F) ? wxString((char)c) : wxString(".");
        return s;
    }
    default:
        break;
    }

    uint8_t bytes[8];
    RegsToBytes(tmp, n, order, bytes);
    uint64_t u = BytesToU64(bytes, n * 2);

    switch (fmt) {
    case DataFmt::Unsigned32:
        return wxString::Format("%lu", (unsigned long)(uint32_t)u);
    case DataFmt::Signed32:
        return wxString::Format("%ld", (long)(int32_t)(uint32_t)u);
    case DataFmt::Float32: {
        uint32_t u32 = (uint32_t)u;
        float f;
        std::memcpy(&f, &u32, sizeof(f));
        return wxString::Format("%.7g", f);
    }
    case DataFmt::Unsigned64:
        return wxString::Format("%llu", (unsigned long long)u);
    case DataFmt::Signed64:
        return wxString::Format("%lld", (long long)(int64_t)u);
    case DataFmt::Float64: {
        double d;
        std::memcpy(&d, &u, sizeof(d));
        return wxString::Format("%.15g", d);
    }
    default:
        return wxEmptyString;
    }
}

bool ParseInt(const wxString& textIn, long long& value)
{
    wxString text = textIn;
    text.Trim(true).Trim(false);
    if (text.empty())
        return false;
    bool neg = false;
    if (text[0] == '-' || text[0] == '+') {
        neg = text[0] == '-';
        text = text.Mid(1);
    }
    text.Replace("_", "");
    unsigned long long u = 0;
    bool ok;
    if (text.StartsWith("0x") || text.StartsWith("0X"))
        ok = text.Mid(2).ToULongLong(&u, 16);
    else if (text.StartsWith("0b") || text.StartsWith("0B"))
        ok = text.Mid(2).ToULongLong(&u, 2);
    else
        ok = text.ToULongLong(&u, 10);
    if (!ok)
        return false;
    value = neg ? -(long long)u : (long long)u;
    return true;
}

bool Parse(const wxString& textIn, DataFmt fmt, WordOrder order, std::vector<uint16_t>& out)
{
    wxString text = textIn;
    text.Trim(true).Trim(false);
    if (text.empty())
        return false;

    int n = RegisterCount(fmt);
    uint64_t u = 0;

    switch (fmt) {
    case DataFmt::Ascii: {
        // Two characters per register (padded with 0).
        wxScopedCharBuffer utf = text.utf8_str();
        std::string s(utf.data(), utf.length());
        if (s.size() > 2)
            return false;
        uint8_t b[2] = {0, 0};
        for (size_t i = 0; i < s.size(); ++i)
            b[i] = (uint8_t)s[i];
        uint16_t r;
        BytesToRegs(b, 1, order, &r);
        out.push_back(r);
        return true;
    }
    case DataFmt::Binary16: {
        wxString t = text;
        t.Replace(" ", "");
        if (t.StartsWith("0b") || t.StartsWith("0B"))
            t = t.Mid(2);
        unsigned long long v;
        if (!t.ToULongLong(&v, 2) || v > 0xFFFF)
            return false;
        out.push_back((uint16_t)v);
        return true;
    }
    case DataFmt::Float32: {
        double d;
        if (!text.ToCDouble(&d))
            return false;
        float f = (float)d;
        uint32_t u32;
        std::memcpy(&u32, &f, sizeof(u32));
        u = u32;
        break;
    }
    case DataFmt::Float64: {
        double d;
        if (!text.ToCDouble(&d))
            return false;
        std::memcpy(&u, &d, sizeof(u));
        break;
    }
    default: {
        long long v;
        if (fmt == DataFmt::Hex16 && !text.Lower().StartsWith("0x"))
            text = "0x" + text;
        if (!ParseInt(text, v))
            return false;
        switch (fmt) {
        case DataFmt::Unsigned16:
        case DataFmt::Hex16:
            if (v < 0 || v > 0xFFFF)
                return false;
            break;
        case DataFmt::Signed16:
            if (v < -32768 || v > 0xFFFF)
                return false;
            break;
        case DataFmt::Unsigned32:
            if (v < 0 || v > 0xFFFFFFFFLL)
                return false;
            break;
        case DataFmt::Signed32:
            if (v < INT32_MIN || v > 0xFFFFFFFFLL)
                return false;
            break;
        default:
            break;
        }
        u = (uint64_t)v;
        break;
    }
    }

    if (n == 1) {
        out.push_back((uint16_t)(u & 0xFFFF));
        return true;
    }
    uint8_t bytes[8];
    for (int i = 0; i < n * 2; ++i)
        bytes[i] = (uint8_t)(u >> (8 * (n * 2 - 1 - i)));
    uint16_t regs[4];
    BytesToRegs(bytes, n, order, regs);
    out.insert(out.end(), regs, regs + n);
    return true;
}

void FromDouble(double v, DataFmt fmt, WordOrder order, std::vector<uint16_t>& out)
{
    auto clamp = [](double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); };
    wxString text;
    switch (fmt) {
    case DataFmt::Float32:
    case DataFmt::Float64:
        text = wxString::FromCDouble(v, 17);
        break;
    case DataFmt::Signed16:
        text = wxString::Format("%lld", (long long)std::llround(clamp(v, -32768, 32767)));
        break;
    case DataFmt::Signed32:
        text = wxString::Format("%lld", (long long)std::llround(clamp(v, INT32_MIN, INT32_MAX)));
        break;
    case DataFmt::Signed64:
        text = wxString::Format("%lld", (long long)std::llround(clamp(v, -9.2e18, 9.2e18)));
        break;
    case DataFmt::Unsigned32:
        text = wxString::Format("%lld", (long long)std::llround(clamp(v, 0, 4294967295.0)));
        break;
    case DataFmt::Unsigned64:
        text = wxString::Format("%llu", (unsigned long long)clamp(std::round(v), 0, 1.8e19));
        break;
    default: // 16-bit unsigned style formats
        text = wxString::Format("0x%llX", (long long)std::llround(clamp(v, 0, 65535)));
        fmt = DataFmt::Unsigned16;
        break;
    }
    size_t before = out.size();
    if (!Parse(text, fmt, order, out))
        out.resize(before + RegisterCount(fmt), 0);
}

bool ParseList(const wxString& text, DataFmt fmt, WordOrder order,
               std::vector<uint16_t>& out, wxString* error)
{
    out.clear();
    if (fmt == DataFmt::Ascii) {
        // Whole text is a string: 2 chars per register.
        wxScopedCharBuffer utf = text.utf8_str();
        std::string s(utf.data(), utf.length());
        if (s.empty()) {
            if (error)
                *error = "empty string";
            return false;
        }
        if (s.size() % 2)
            s.push_back('\0');
        for (size_t i = 0; i < s.size(); i += 2) {
            uint8_t b[2] = {(uint8_t)s[i], (uint8_t)s[i + 1]};
            uint16_t r;
            BytesToRegs(b, 1, order, &r);
            out.push_back(r);
        }
        return true;
    }
    wxString delims = (fmt == DataFmt::Binary16) ? ",;\t\n" : ", ;\t\n";
    wxStringTokenizer tok(text, delims, wxTOKEN_STRTOK);
    while (tok.HasMoreTokens()) {
        wxString t = tok.GetNextToken();
        if (!Parse(t, fmt, order, out)) {
            if (error)
                *error = wxString::Format("invalid value '%s'", t);
            return false;
        }
    }
    if (out.empty()) {
        if (error)
            *error = "no values";
        return false;
    }
    return true;
}

bool ParseBits(const wxString& text, std::vector<uint8_t>& out, wxString* error)
{
    out.clear();
    wxStringTokenizer tok(text, ", ;\t\n", wxTOKEN_STRTOK);
    while (tok.HasMoreTokens()) {
        wxString t = tok.GetNextToken().Lower();
        if (t == "1" || t == "on" || t == "true")
            out.push_back(1);
        else if (t == "0" || t == "off" || t == "false")
            out.push_back(0);
        else {
            if (error)
                *error = wxString::Format("invalid bit value '%s'", t);
            return false;
        }
    }
    if (out.empty()) {
        if (error)
            *error = "no values";
        return false;
    }
    return true;
}

wxString HexDump(const uint8_t* data, int len)
{
    wxString s;
    s.reserve(len * 3);
    for (int i = 0; i < len; ++i) {
        if (i)
            s += ' ';
        s += wxString::Format("%02X", data[i]);
    }
    return s;
}

bool ParseHexBytes(const wxString& text, std::vector<uint8_t>& out)
{
    out.clear();
    wxString t;
    for (wxUniChar c : text) {
        if (wxIsxdigit(c))
            t += c;
        else if (c == ' ' || c == ',' || c == '\t' || c == '\n' || c == ':' || c == '-')
            t += ' ';
        else
            return false;
    }
    wxStringTokenizer tok(t, " ", wxTOKEN_STRTOK);
    while (tok.HasMoreTokens()) {
        wxString h = tok.GetNextToken();
        if (h.length() % 2)
            h = "0" + h;
        for (size_t i = 0; i < h.length(); i += 2) {
            unsigned long v;
            if (!h.Mid(i, 2).ToULong(&v, 16))
                return false;
            out.push_back((uint8_t)v);
        }
    }
    return !out.empty();
}

} // namespace DataFormat
