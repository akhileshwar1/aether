#pragma once
// ring_mmap.h
// Byte-framed mmap ring (producer API + C bindings).
// Producer writes frames: [uint32_t len][uint8_t type][payload...]
// len = (1 + payload_len). type: 1 = DEPTH_UPDATE, 2 = SNAPSHOT
// NOTE: the extern helps us expose the "interface" in the C ABI way which is understood by ocaml.
//       the "internals" though can be implemented in c++ way. AN ABI basically means the way a 
//       languages uses the CPU, like the calling convention, register usage, naming etc.
#include <cstdint>
#include <cstddef>

extern "C" {
  struct RingHandleC;
}

namespace aether { namespace ring {

  // header in mmap
  struct RingHeader {
    uint32_t magic;      // 0xAETHER02
    uint16_t version;    // layout version
    uint16_t reserved0;
    uint64_t buf_size;   // size of circular buffer region in bytes
    uint64_t reserved[4];
  } __attribute__((packed));

  // opaque C++ handle
  struct RingHandle;

  // create or open
  RingHandle* create_ring(const char *path, size_t buf_size);
  RingHandle* open_ring(const char *path);
  void close_ring(RingHandle *h);

  // publish a framed message (type + payload). returns true on success.
  // msg_type: 1 = DEPTH_UPDATE, 2 = SNAPSHOT, user-defined types ok
  bool publish_message(RingHandle *h, uint8_t msg_type, const void *payload, size_t payload_len);

  // convenience: publish a null-terminated JSON string as snapshot
  bool publish_snapshot_json(RingHandle *h, const char *json_cstr);

  // helpers
  uint64_t ring_head(const RingHandle *h);
  uint64_t ring_tail(const RingHandle *h);
  uint64_t ring_buf_size(const RingHandle *h);

  // C bindings
  extern "C" {
    struct RingHandleC {
      RingHandle *h;
    };

    struct RingHandleC* ring_create(const char *path, size_t buf_size);
    struct RingHandleC* ring_open(const char *path);
    void ring_close(struct RingHandleC* ch);
    int ring_publish(struct RingHandleC* ch, unsigned int msg_type, const void* payload, size_t payload_len);
    int ring_publish_snapshot_json(struct RingHandleC* ch, const char* json_cstr);
    uint64_t ring_get_head(struct RingHandleC* ch);
    uint64_t ring_get_tail(struct RingHandleC* ch);
    uint64_t ring_get_buf_size(struct RingHandleC* ch);
    void* ring_get_buffer_ptr(struct RingHandleC* ch);
    void ring_set_tail(struct RingHandleC* ch, uint64_t new_tail);
  } // extern "C"

}} // namespace
