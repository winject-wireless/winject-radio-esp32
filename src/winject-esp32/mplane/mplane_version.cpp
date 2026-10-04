#include "mplane_version.h"

#include <ctype.h>

namespace
{
bool parse_u8_component(const char** text, uint8_t* out)
{
    if (**text == '\0' || !isdigit(static_cast<unsigned char>(**text)))
    {
        return false;
    }
    unsigned long value = 0;
    while (**text != '\0' && isdigit(static_cast<unsigned char>(**text)))
    {
        value = value * 10u + static_cast<unsigned long>(**text - '0');
        if (value > 255u)
        {
            return false;
        }
        ++*text;
    }
    *out = static_cast<uint8_t>(value);
    return true;
}

bool parse_u16_component(const char** text, uint16_t* out)
{
    if (**text == '\0' || !isdigit(static_cast<unsigned char>(**text)))
    {
        return false;
    }
    unsigned long value = 0;
    while (**text != '\0' && isdigit(static_cast<unsigned char>(**text)))
    {
        value = value * 10u + static_cast<unsigned long>(**text - '0');
        if (value > 65535u)
        {
            return false;
        }
        ++*text;
    }
    *out = static_cast<uint16_t>(value);
    return true;
}
}  // namespace

bool mplane_parse_version(const char* text, uint8_t* x, uint8_t* y, uint16_t* z)
{
    if (text == nullptr || x == nullptr || y == nullptr || z == nullptr)
    {
        return false;
    }
    if (*text != 'v' && *text != 'V')
    {
        return false;
    }
    ++text;
    if (!parse_u8_component(&text, x))
    {
        return false;
    }
    if (*text != '.')
    {
        return false;
    }
    ++text;
    if (!parse_u8_component(&text, y))
    {
        return false;
    }
    if (*text != '.')
    {
        return false;
    }
    ++text;
    if (!parse_u16_component(&text, z))
    {
        return false;
    }
    return *text == '\0';
}
