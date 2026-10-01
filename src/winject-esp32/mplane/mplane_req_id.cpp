#include "mplane_req_id.h"

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

char* trim(char* s)
{
    while (is_blank(*s))
    {
        ++s;
    }
    if (*s == '\0')
    {
        return s;
    }
    char* end = s + strlen(s) - 1;
    while (end > s && is_blank(*end))
    {
        *end-- = '\0';
    }
    return s;
}
}  // namespace

bool mplane_peel_cmd_prefix(char** line, uint8_t* req_id, bool* have_req_id)
{
    if (line == nullptr || req_id == nullptr || have_req_id == nullptr)
    {
        return false;
    }
    *have_req_id = false;
    char* text = trim(*line);
    if (strncasecmp(text, "cmd:", 4) != 0)
    {
        *line = text;
        return true;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed =
        strtoul(text + 4, &end, 10);
    if (errno != 0 || end == text + 4 || parsed > 255u)
    {
        return false;
    }
    const char* p = end;
    if (*p != '\0' && !is_blank(*p))
    {
        return false;
    }
    while (is_blank(*p))
    {
        ++p;
    }
    if (*p == '\0')
    {
        return false;
    }
    *req_id = static_cast<uint8_t>(parsed);
    *have_req_id = true;
    *line = const_cast<char*>(p);
    return true;
}

void mplane_decorate_reply_line(char* line, size_t line_cap, uint8_t req_id)
{
    if (line == nullptr || line_cap == 0)
    {
        return;
    }
    char* text = trim(line);
    char decorated[512];

    if (strcmp(text, "OK") == 0)
    {
        snprintf(decorated, sizeof(decorated), "OK:%u",
                 static_cast<unsigned>(req_id));
    }
    else if (strncmp(text, "OK ", 3) == 0)
    {
        snprintf(decorated, sizeof(decorated), "OK:%u %s",
                 static_cast<unsigned>(req_id), text + 3);
    }
    else if (strncmp(text, "NOK ", 4) == 0)
    {
        snprintf(decorated, sizeof(decorated), "NOK:%u %s",
                 static_cast<unsigned>(req_id), text + 4);
    }
    else if (strcmp(text, "NOK") == 0)
    {
        snprintf(decorated, sizeof(decorated), "NOK:%u EINVAL",
                 static_cast<unsigned>(req_id));
    }
    else
    {
        snprintf(decorated, sizeof(decorated), "OK:%u %s",
                 static_cast<unsigned>(req_id), text);
    }

    snprintf(line, line_cap, "%s", decorated);
}

mplane_req_id_reply::mplane_req_id_reply(mplane_reply& inner, uint8_t req_id)
    : inner_(inner), req_id_(req_id)
{
}

void mplane_req_id_reply::write(const char* data, size_t n)
{
    if (data == nullptr || n == 0)
    {
        return;
    }
    for (size_t i = 0; i < n; ++i)
    {
        const char c = data[i];
        if (c == '\n')
        {
            if (pending_len_ > 0)
            {
                pending_[pending_len_] = '\0';
                mplane_decorate_reply_line(pending_, sizeof(pending_), req_id_);
                inner_.write(pending_, strlen(pending_));
                pending_len_ = 0;
            }
            inner_.write("\n", 1);
            continue;
        }
        if (pending_len_ + 1 >= sizeof(pending_))
        {
            inner_.write(pending_, pending_len_);
            pending_len_ = 0;
        }
        pending_[pending_len_++] = c;
    }
}
