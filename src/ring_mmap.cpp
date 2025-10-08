// ring_mmap.cpp
#include "ring_mmap.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <chrono>

namespace aether { namespace ring {

  static constexpr uint32_t RING_MAGIC =
    (uint32_t('A') << 24) | (uint32_t('E') << 16) |
    (uint32_t('T') << 8)  | uint32_t('H'); // "AETH"
  static constexpr uint16_t RING_VERSION = 1;
  static constexpr uint32_t WRAP_MARKER = 0xFFFFFFFFu;

  struct RingHandle {
    int fd;
    size_t file_size;
    void *map_base;              // base of mmap
    RingHeader *hdr;
    std::atomic<uint64_t> *head; // absolute byte offset of next free byte to write
    std::atomic<uint64_t> *tail; // absolute byte offset of next unread byte by consumer
    void *buf_base;              // start of circular buffer region
    uint64_t buf_size;           // convenience copy from header
  };

  // page align helper
  static size_t page_round_up(size_t s) {
    size_t p = (size_t)sysconf(_SC_PAGESIZE);
    return ((s + p - 1) / p) * p;
  }

  static uint64_t now_us() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
  }

  // layout:
  // [RingHeader][uint64_t head][uint64_t tail][padding (to page)][circular buffer (buf_size bytes)]
  // head/tail are atomics placed in mmap region
  RingHandle* create_ring(const char *path, size_t buf_size) {
    if (!path || buf_size < 4096) {
      std::cerr << "[ring] create_ring: invalid args\n";
      return nullptr;
    }
    size_t header_sz = sizeof(RingHeader);
    size_t atomics_sz = sizeof(uint64_t) * 2;
    size_t meta_pad = 64; // pad so buffer starts at comfy offset
    size_t total = header_sz + atomics_sz + meta_pad + buf_size;
    size_t total_mmap = page_round_up(total);

    int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
      std::cerr << "[ring] create open failed: " << strerror(errno) << "\n";
      return nullptr;
    }
    if (ftruncate(fd, (off_t)total_mmap) != 0) {
      std::cerr << "[ring] ftruncate failed: " << strerror(errno) << "\n";
      close(fd); return nullptr;
    }
    void *m = mmap(nullptr, total_mmap, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) {
      std::cerr << "[ring] mmap failed: " << strerror(errno) << "\n";
      close(fd); return nullptr;
    }
    std::memset(m, 0, total_mmap);

    RingHandle *h = new RingHandle();
    h->fd = fd; h->file_size = total_mmap; h->map_base = m;
    h->hdr = reinterpret_cast<RingHeader*>(m);
    h->hdr->magic = RING_MAGIC;
    h->hdr->version = RING_VERSION;
    h->hdr->buf_size = (uint64_t)buf_size;

    uint8_t *p = reinterpret_cast<uint8_t*>(m) + header_sz;
    // placement-new atomic head/tail
    h->head = new (p) std::atomic<uint64_t>(0);
    h->tail = new (p + sizeof(uint64_t)) std::atomic<uint64_t>(0);

    h->buf_base = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(m) + header_sz + atomics_sz + meta_pad);
    h->buf_size = buf_size;

    std::cerr << "[ring] created ring " << path << " mmap=" << total_mmap << " buf_size=" << buf_size << "\n";
    return h;
  }

  RingHandle* open_ring(const char *path) {
    if (!path) return nullptr;
    int fd = open(path, O_RDWR, 0600);
    if (fd < 0) { std::cerr << "[ring] open failed: " << strerror(errno) << "\n"; return nullptr; }
    struct stat st;
    if (fstat(fd, &st) != 0) { std::cerr << "[ring] fstat failed\n"; close(fd); return nullptr; }
    size_t total_mmap = (size_t)st.st_size;
    void *m = mmap(nullptr, total_mmap, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { std::cerr << "[ring] mmap open failed: " << strerror(errno) << "\n"; close(fd); return nullptr; }
    RingHeader *hdr = reinterpret_cast<RingHeader*>(m);
    if (hdr->magic != RING_MAGIC) { std::cerr << "[ring] magic mismatch\n"; munmap(m, total_mmap); close(fd); return nullptr; }
    RingHandle *h = new RingHandle();
    h->fd = fd; h->file_size = total_mmap; h->map_base = m; h->hdr = hdr;
    size_t header_sz = sizeof(RingHeader);
    uint8_t *p = reinterpret_cast<uint8_t*>(m) + header_sz;
    h->head = reinterpret_cast<std::atomic<uint64_t>*>(p);
    h->tail = reinterpret_cast<std::atomic<uint64_t>*>(p + sizeof(uint64_t));
    size_t atomics_sz = sizeof(uint64_t)*2;
    size_t meta_pad = 64;
    h->buf_base = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(m) + header_sz + atomics_sz + meta_pad);
    h->buf_size = (uint64_t)hdr->buf_size;
    std::cerr << "[ring] opened ring " << path << " buf_size=" << h->buf_size << "\n";
    return h;
  }

  void close_ring(RingHandle *h) {
    if (!h) return;
    munmap(h->map_base, h->file_size);
    close(h->fd);
    delete h;
  }

  uint64_t ring_head(const RingHandle *h) { return h ? h->head->load(std::memory_order_acquire) : 0; }
  uint64_t ring_tail(const RingHandle *h) { return h ? h->tail->load(std::memory_order_acquire) : 0; }
  uint64_t ring_buf_size(const RingHandle *h) { return h ? h->buf_size : 0; }

  // publish framed message with wrap-on-need. Overwrite-oldest policy by advancing tail if needed.
  bool publish_message(RingHandle *h, uint8_t msg_type, const void *payload, size_t payload_len) {
    if (!h) return false;
    if (payload_len > (size_t)h->buf_size) return false; // too big

    uint32_t msg_len = (uint32_t)(1 + payload_len); // type + payload
    uint64_t need = (uint64_t)4 + msg_len; // length field + msg_len

    // load positions
    uint64_t head = h->head->load(std::memory_order_relaxed);
    uint64_t tail = h->tail->load(std::memory_order_acquire);
    uint64_t used = head - tail;
    if (need > h->buf_size) return false;

    if (need > h->buf_size - used) {
      // drop oldest bytes to make room (advance tail)
      uint64_t want_free = need - (h->buf_size - used);
      tail += want_free;
      h->tail->store(tail, std::memory_order_release);
      // note: consumer will see tail advanced and will skip messages (lost)
    }

    uint64_t pos = head % h->buf_size;
    uint8_t *buf = reinterpret_cast<uint8_t*>(h->buf_base);

    if (pos + need <= h->buf_size) {
      // one contiguous write
      uint32_t len_field = msg_len;
      std::memcpy(buf + pos, &len_field, sizeof(uint32_t));
      buf[pos + 4] = msg_type;
      if (payload_len) std::memcpy(buf + pos + 5, payload, payload_len);
    } else {
      // need wrap: write WRAP_MARKER at pos, then write frame at 0
      uint32_t wm = WRAP_MARKER;
      if (pos + sizeof(uint32_t) <= h->buf_size) {
        std::memcpy(buf + pos, &wm, sizeof(uint32_t));
      } else {
        // theoretically pos+4 cannot exceed buf_size because we tested pos+need>buf_size and sizeof(uint32_t)=4 <= need
        // but defensive
        size_t part = h->buf_size - pos;
        std::memcpy(buf + pos, &wm, part);
        std::memcpy(buf, reinterpret_cast<uint8_t*>(&wm) + part, sizeof(uint32_t)-part);
      }
      // write frame at start
      uint32_t len_field = msg_len;
      std::memcpy(buf + 0, &len_field, sizeof(uint32_t));
      buf[4] = msg_type;
      if (payload_len) std::memcpy(buf + 5, payload, payload_len);
      // Note: remaining bytes after payload at end of frame are left as-is or zeroed optional
    }

    // release fence to ensure buffer writes visible before advancing head
    std::atomic_thread_fence(std::memory_order_release);
    h->head->store(head + need, std::memory_order_release);
    return true;
  }

  bool publish_snapshot_json(RingHandle *h, const char *json_cstr) {
    if (!h || !json_cstr) return false;
    size_t len = strlen(json_cstr);
    return publish_message(h, 2 /*SNAPSHOT*/, json_cstr, len);
  }

  // C bindings
  extern "C" {

    RingHandleC* ring_create(const char *path, size_t buf_size) {
      RingHandle *h = create_ring(path, buf_size);
      if (!h) return nullptr;
      RingHandleC *c = (RingHandleC*)malloc(sizeof(RingHandleC));
      c->h = h; return c;
    }
    RingHandleC* ring_open(const char *path) {
      RingHandle *h = open_ring(path);
      if (!h) return nullptr;
      RingHandleC *c = (RingHandleC*)malloc(sizeof(RingHandleC));
      c->h = h; return c;
    }
    void ring_close(RingHandleC* ch) {
      if (!ch) return;
      close_ring(ch->h);
      free(ch);
    }
    int ring_publish(RingHandleC* ch, unsigned int msg_type, const void* payload, size_t payload_len) {
      if (!ch) return 0;
      bool ok = publish_message(ch->h, (uint8_t)msg_type, payload, payload_len);
      return ok ? 1 : 0;
    }
    int ring_publish_snapshot_json(RingHandleC* ch, const char* json_cstr) {
      if (!ch) return 0;
      bool ok = publish_snapshot_json(ch->h, json_cstr);
      return ok ? 1 : 0;
    }
    uint64_t ring_get_head(RingHandleC* ch) { return ch ? ch->h->head->load(std::memory_order_acquire) : 0; }
    uint64_t ring_get_tail(RingHandleC* ch) { return ch ? ch->h->tail->load(std::memory_order_acquire) : 0; }
    uint64_t ring_get_buf_size(RingHandleC* ch) { return ch ? ch->h->buf_size : 0; }

  } // extern C

}} // namespace aether::ring
