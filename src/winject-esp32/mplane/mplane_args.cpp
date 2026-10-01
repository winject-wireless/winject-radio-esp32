#include "mplane_args.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{
bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

int hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

// Parses one dotted-quad octet; advances *p past the digits.
bool parse_octet(const char** p, uint8_t* out)
{
    const char* s = *p;
    unsigned value = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9')
    {
        value = value * 10u + static_cast<unsigned>(*s - '0');
        ++digits;
        ++s;
        if (digits > 3 || value > 255u)
        {
            return false;
        }
    }
    if (digits == 0)
    {
        return false;
    }
    *out = static_cast<uint8_t>(value);
    *p = s;
    return true;
}

// Parses a.b.c.d at text; returns a pointer past the address or nullptr.
const char* parse_ipv4_prefix_part(const char* text, uint32_t* out)
{
    uint8_t octets[4] = {};
    const char* p = text;
    for (int i = 0; i < 4; ++i)
    {
        if (!parse_octet(&p, &octets[i]))
        {
            return nullptr;
        }
        if (i < 3)
        {
            if (*p != '.')
            {
                return nullptr;
            }
            ++p;
        }
    }
    memcpy(out, octets, sizeof(octets));
    return p;
}
}  // namespace

bool mplane_parse_uint(const char* text, unsigned long min, unsigned long max,
                       unsigned long* out)
{
    if (text == nullptr || out == nullptr || *text < '0' || *text > '9')
    {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long v = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || v < min || v > max)
    {
        return false;
    }
    *out = v;
    return true;
}

bool mplane_parse_int(const char* text, long min, long max, long* out)
{
    if (text == nullptr || out == nullptr || *text == '\0')
    {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long v = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || v < min || v > max)
    {
        return false;
    }
    *out = v;
    return true;
}

bool mplane_parse_bool(const char* text, bool* out)
{
    if (text == nullptr || out == nullptr)
    {
        return false;
    }
    if (strcasecmp(text, "1") == 0 || strcasecmp(text, "true") == 0 ||
        strcasecmp(text, "on") == 0 || strcasecmp(text, "yes") == 0)
    {
        *out = true;
        return true;
    }
    if (strcasecmp(text, "0") == 0 || strcasecmp(text, "false") == 0 ||
        strcasecmp(text, "off") == 0 || strcasecmp(text, "no") == 0)
    {
        *out = false;
        return true;
    }
    return false;
}

bool mplane_parse_ipv4(const char* text, uint32_t* out)
{
    if (text == nullptr || out == nullptr)
    {
        return false;
    }
    uint32_t ip = 0;
    const char* end = parse_ipv4_prefix_part(text, &ip);
    if (end == nullptr || *end != '\0')
    {
        return false;
    }
    *out = ip;
    return true;
}

bool mplane_parse_ipv4_prefix(const char* text, uint32_t* ip, uint8_t* prefix,
                              bool* has_prefix)
{
    if (text == nullptr || ip == nullptr || prefix == nullptr ||
        has_prefix == nullptr)
    {
        return false;
    }
    uint32_t addr = 0;
    const char* end = parse_ipv4_prefix_part(text, &addr);
    if (end == nullptr)
    {
        return false;
    }
    if (*end == '\0')
    {
        *ip = addr;
        *has_prefix = false;
        return true;
    }
    unsigned long bits = 0;
    if (*end != '/' || !mplane_parse_uint(end + 1, 0, 32, &bits))
    {
        return false;
    }
    *ip = addr;
    *prefix = static_cast<uint8_t>(bits);
    *has_prefix = true;
    return true;
}

bool mplane_parse_mac(const char* text, uint8_t mac[6])
{
    if (text == nullptr || mac == nullptr)
    {
        return false;
    }
    const size_t n = strlen(text);
    const bool separated = n == 17;
    if (!separated && n != 12)
    {
        return false;
    }
    const char sep = separated ? text[2] : '\0';
    if (separated && sep != ':' && sep != '-')
    {
        return false;
    }
    uint8_t out[6] = {};
    const size_t stride = separated ? 3 : 2;
    for (size_t i = 0; i < 6; ++i)
    {
        const char* p = text + i * stride;
        const int hi = hex_value(p[0]);
        const int lo = hex_value(p[1]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }
        if (separated && i < 5 && p[2] != sep)
        {
            return false;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    memcpy(mac, out, sizeof(out));
    return true;
}

void mplane_format_ipv4(uint32_t ip, char* out, size_t out_n)
{
    const auto* b = reinterpret_cast<const uint8_t*>(&ip);
    snprintf(out, out_n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

void mplane_format_mac(const uint8_t mac[6], char* out, size_t out_n)
{
    snprintf(out, out_n, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
}

bool mplane_args::parse(char* text)
{
    count_ = 0;
    if (text == nullptr)
    {
        return true;
    }
    char* p = text;
    while (true)
    {
        while (is_blank(*p))
        {
            ++p;
        }
        if (*p == '\0')
        {
            return true;
        }
        if (count_ >= k_max)
        {
            return false;
        }
        char* key = p;
        while (*p != '\0' && !is_blank(*p) && *p != '=')
        {
            ++p;
        }
        if (*p != '=' || p == key)
        {
            return false;
        }
        *p++ = '\0';
        char* value = p;
        while (*p != '\0' && !is_blank(*p))
        {
            ++p;
        }
        if (*p != '\0')
        {
            *p++ = '\0';
        }
        if (find(key) != nullptr)
        {
            return false;
        }
        entries_[count_++] = {key, value};
    }
}

const char* mplane_args::find(const char* key) const
{
    for (size_t i = 0; i < count_; ++i)
    {
        if (strcasecmp(entries_[i].key, key) == 0)
        {
            return entries_[i].value;
        }
    }
    return nullptr;
}

bool mplane_args::only(const char* const* allowed) const
{
    for (size_t i = 0; i < count_; ++i)
    {
        bool known = false;
        for (const char* const* a = allowed; *a != nullptr; ++a)
        {
            if (strcasecmp(entries_[i].key, *a) == 0)
            {
                known = true;
                break;
            }
        }
        if (!known)
        {
            return false;
        }
    }
    return true;
}

mplane_args::status mplane_args::get_uint(const char* key, unsigned long min,
                                          unsigned long max,
                                          unsigned long* out) const
{
    const char* v = find(key);
    if (v == nullptr)
    {
        return status::absent;
    }
    return mplane_parse_uint(v, min, max, out) ? status::ok : status::invalid;
}

mplane_args::status mplane_args::get_int(const char* key, long min, long max,
                                         long* out) const
{
    const char* v = find(key);
    if (v == nullptr)
    {
        return status::absent;
    }
    return mplane_parse_int(v, min, max, out) ? status::ok : status::invalid;
}

mplane_args::status mplane_args::get_bool(const char* key, bool* out) const
{
    const char* v = find(key);
    if (v == nullptr)
    {
        return status::absent;
    }
    return mplane_parse_bool(v, out) ? status::ok : status::invalid;
}
