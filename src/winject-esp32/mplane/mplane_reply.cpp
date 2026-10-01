#include "mplane_reply.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char* mplane_status_code(mplane_status status)
{
    switch (status)
    {
        case mplane_status::ok:
            return "OK";
        case mplane_status::invalid:
            return "EINVAL";
        case mplane_status::already:
            return "EALREADY";
        case mplane_status::stale:
            return "ESTALE";
        case mplane_status::no_device:
            return "ENODEV";
        case mplane_status::io_error:
            return "EIO";
        case mplane_status::not_found:
            return "ENOENT";
        case mplane_status::unsupported:
            return "ENOTSUP";
    }
    return "EINVAL";
}

void mplane_reply::write(const char* text)
{
    if (text != nullptr)
    {
        write(text, strlen(text));
    }
}

void mplane_reply::print(const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
    {
        return;
    }
    const size_t len =
        static_cast<size_t>(n) < sizeof(buf) ? static_cast<size_t>(n)
                                             : sizeof(buf) - 1;
    write(buf, len);
}

void mplane_reply::ok()
{
    write("OK\n");
}

void mplane_reply::nok(mplane_status status)
{
    nok(mplane_status_code(status));
}

void mplane_reply::nok(const char* code)
{
    print("NOK %s\n", code != nullptr ? code : "EINVAL");
}
