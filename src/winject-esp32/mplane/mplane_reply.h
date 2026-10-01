#ifndef WINJECT_MPLANE_MPLANE_REPLY_H_
#define WINJECT_MPLANE_MPLANE_REPLY_H_

#include "mplane_backend.h"

#include <stddef.h>

// Sink for one command's reply text. Replies are lines: `OK [...]`,
// `NOK <code>`, or an info line such as `tx_info ...`.
class mplane_reply
{
public:
    virtual ~mplane_reply() = default;

    virtual void write(const char* data, size_t n) = 0;

    void write(const char* text);
    void print(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void ok();
    void nok(mplane_status status);
    void nok(const char* code);
};

#endif  // WINJECT_MPLANE_MPLANE_REPLY_H_
