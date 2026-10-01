#ifndef WINJECT_MPLANE_MPLANE_REQ_ID_H_
#define WINJECT_MPLANE_MPLANE_REQ_ID_H_

#include "mplane_reply.h"

#include <stddef.h>
#include <stdint.h>

// If the line starts with `cmd:<u8>`, strips that prefix and sets req_id.
// Lines without the prefix are unchanged (have_req_id=false). Malformed `cmd:`
// returns false.
bool mplane_peel_cmd_prefix(char** line, uint8_t* req_id, bool* have_req_id);

// Prefixes one reply line with `OK:<id>` or `NOK:<id>` (without newline).
void mplane_decorate_reply_line(char* line, size_t line_cap, uint8_t req_id);

// Forwards writes to inner and decorates each completed line with req_id.
class mplane_req_id_reply : public mplane_reply
{
public:
    mplane_req_id_reply(mplane_reply& inner, uint8_t req_id);

    void write(const char* data, size_t n) override;

private:
    mplane_reply& inner_;
    uint8_t req_id_;
    char pending_[512];
    size_t pending_len_ = 0;
};

#endif  // WINJECT_MPLANE_MPLANE_REQ_ID_H_
