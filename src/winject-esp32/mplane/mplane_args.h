#ifndef WINJECT_MPLANE_MPLANE_ARGS_H_
#define WINJECT_MPLANE_MPLANE_ARGS_H_

#include <stddef.h>
#include <stdint.h>

// Value parsers for m-plane arguments. All return false (out untouched) on
// malformed or out-of-range input. IPv4 values are network byte order.
bool mplane_parse_uint(const char* text, unsigned long min, unsigned long max,
                       unsigned long* out);
bool mplane_parse_int(const char* text, long min, long max, long* out);
// true/false, 1/0, on/off, yes/no (case-insensitive).
bool mplane_parse_bool(const char* text, bool* out);
// Dotted quad only (no DNS: handlers run on the reactor task).
bool mplane_parse_ipv4(const char* text, uint32_t* out);
// a.b.c.d/<0-32>; has_prefix=false when the /prefix suffix is absent.
bool mplane_parse_ipv4_prefix(const char* text, uint32_t* ip, uint8_t* prefix,
                              bool* has_prefix);
// aa:bb:cc:dd:ee:ff, aa-bb-..., or 12 hex digits.
bool mplane_parse_mac(const char* text, uint8_t mac[6]);

// out_n >= 16.
void mplane_format_ipv4(uint32_t ip, char* out, size_t out_n);
// out_n >= 18; lower-case colon form.
void mplane_format_mac(const uint8_t mac[6], char* out, size_t out_n);

// The `key=value ...` arguments of one command. Keys are case-insensitive;
// keys and values point into the parsed (modified in place) text, which must
// outlive this object.
class mplane_args
{
public:
    enum class status
    {
        absent,
        ok,
        invalid,
    };

    static constexpr size_t k_max = 8;

    // false on a token without '=', an empty key, a duplicate key, or more
    // than k_max tokens. Values may be empty (`addr=`).
    bool parse(char* text);

    size_t size() const
    {
        return count_;
    }
    bool empty() const
    {
        return count_ == 0;
    }

    // nullptr when absent; "" when present with an empty value.
    const char* find(const char* key) const;
    // True when every parsed key is in `allowed` (nullptr-terminated).
    bool only(const char* const* allowed) const;

    status get_uint(const char* key, unsigned long min, unsigned long max,
                    unsigned long* out) const;
    status get_int(const char* key, long min, long max, long* out) const;
    status get_bool(const char* key, bool* out) const;

private:
    struct entry
    {
        const char* key;
        const char* value;
    };

    entry entries_[k_max] = {};
    size_t count_ = 0;
};

#endif  // WINJECT_MPLANE_MPLANE_ARGS_H_
