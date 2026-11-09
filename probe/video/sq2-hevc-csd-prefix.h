// Compatibility for separate Annex-B HEVC CSD and complete AUs.
// No hardware state, input ownership or timestamps live in this byte accumulator.
#ifndef SQ2_HEVC_CSD_PREFIX_H
#define SQ2_HEVC_CSD_PREFIX_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

class Sq2HevcCsdPrefix {
public:
    enum Result { Pass, Stashed, Joined, BadInput, NoSpace, NoMemory };
    Sq2HevcCsdPrefix() : data_(NULL), size_(0), capacity_(0) {}
    ~Sq2HevcCsdPrefix() { free(data_); }
    size_t size() const { return size_; }
    void reset() { size_ = 0; }
    Result consume(uint8_t *dst, size_t dst_capacity,
                   const uint8_t *src, size_t length,
                   bool config, bool eos, size_t *joined_length) {
        if (!joined_length) return BadInput;
        *joined_length = length;
        if (!config && !size_) return Pass;
        if (!config && eos && !length) {
            // No picture was accumulated; preserve the owner's real empty-EOS STOP path.
            reset();
            return Pass;
        }
        if ((!src && length) || !dst || !dst_capacity) return BadInput;
        if (config) {
            // Config-only EOS must not become a fabricated decoded EOS.
            if (eos) return BadInput;
            if (size_ > dst_capacity || length > dst_capacity - size_) return NoSpace;
            const size_t wanted = size_ + length;
            if (wanted > capacity_) {
                void *next = realloc(data_, wanted);
                if (!next) return NoMemory;
                data_ = static_cast<uint8_t *>(next);
                capacity_ = wanted;
            }
            if (length) memcpy(data_ + size_, src, length);
            size_ = wanted;
            return Stashed;
        }
        // shortcut: only separate Annex-B headers plus the existing complete-AU
        // input contract; arbitrary byte fragments need the full upstream parser.
        if (size_ > dst_capacity || length > dst_capacity - size_) return NoSpace;
        if (!annexb(data_, size_, true) || !annexb(src, length, false)) return BadInput;
        memmove(dst + size_, src, length);
        memcpy(dst, data_, size_);
        *joined_length = size_ + length;
        // The owner commits/reset only after a real successful QBUF.
        return Joined;
    }
private:
    Sq2HevcCsdPrefix(const Sq2HevcCsdPrefix &);
    Sq2HevcCsdPrefix &operator=(const Sq2HevcCsdPrefix &);
    uint8_t *data_;
    size_t size_, capacity_;
    static size_t start_code(const uint8_t *p, size_t n, size_t from) {
        for (size_t i = from; i < n && n - i >= 3; ++i)
            if (!p[i] && !p[i + 1] && p[i + 2] == 1) return i;
        return n;
    }
    static bool annexb(const uint8_t *p, size_t n, bool headers) {
        if (!p || n < 6) return false;
        size_t code = start_code(p, n, 0);
        if (code == n) return false;
        for (size_t i = 0; i < code; ++i) if (p[i]) return false;
        bool vcl = false;
        do {
            const size_t nal = code + 3;
            const size_t next = start_code(p, n, nal);
            if (next - nal < 3 || (p[nal] & 0x80) || !(p[nal + 1] & 7)) return false;
            const unsigned type = (p[nal] >> 1) & 63;
            if (headers) {
                if (type != 32 && type != 33 && type != 34 && type != 35 &&
                    type != 39 && type != 40) return false;
            } else if (type <= 31) {
                vcl = true;
            }
            code = next;
        } while (code != n);
        return headers || vcl;
    }
};
#endif
