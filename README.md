# sysipc

A lock-free single-producer/single-consumer ring buffer for inter-process
communication over shared memory, written in C11, with a Linux kernel module
that serves the same mechanism through a character device.

Two unrelated processes map the same physical pages and exchange fixed-size
messages with no syscalls on the data path. Synchronization lives entirely
in the shared region as atomic head/tail indices with acquire/release
ordering — no locks, no kernel involvement after setup.

The project has two backends. The userspace one maps a POSIX shared-memory
object; the kernel one maps pages allocated by `sysipc.ko` and exposed via
`/dev/sysipc`. Both use the same `ring.h`, the same atomics, and the same
benchmark harness.

## Design

- **SPSC lock-free ring.** The producer is the only writer of `head`; the
  consumer is the only writer of `tail`. This ownership split is what allows
  correctness without a mutex.
- **Release/acquire pairing.** The producer's release store on `head`
  publishes the payload written before it; the consumer's acquire load of
  `head` guarantees it sees that payload. The consumer's release on `tail`
  pairs with the producer's acquire, guaranteeing a slot is fully read
  before being overwritten.
- **No pointers in the shared region.** The two processes map the region at
  different virtual addresses, so all addressing is index- and offset-based.
- **Monotonic indices.** `head` and `tail` increment without wrapping and are
  masked at use. Unsigned modular arithmetic makes empty (`head == tail`) and
  full (`head - tail == CAPACITY`) unambiguous.
- **Power-of-two capacity**, so wraparound is a bitmask rather than a modulo.

## Results

4 vCPU Multipass VM (VirtualBox) on Windows, Ubuntu 24.04, gcc 13, `-O2`.
64-byte payloads, 1024-slot ring, 10M messages. Medians over 20 runs;
range in parentheses.

### Throughput

| Transport | Mmsg/s | ns/msg | Total time |
|---|---|---|---|
| sysipc ring | 23.3 | 43 | 430 ms |
| Unix domain socket | 1.62 | 618 | 6184 ms |

**14.4x higher throughput.**

### Round-trip latency (ping-pong, 100k samples)

| Transport | p50 | p99 | p99/p50 |
|---|---|---|---|
| sysipc ring | 252 ns | 384 ns | 1.5 |
| Unix domain socket | 10894 ns | 55660 ns | 5.1 |

**43x lower median latency, 145x lower p99.**

`AF_UNIX` is a deliberately generous baseline — it bypasses the network
stack entirely, making it the fastest socket Linux offers. TCP loopback
would flatter these numbers further.

One-way latency is roughly half the round trip (~120 ns), against ~35 ns
per message in streaming mode. The gap is pipelining: under continuous load
the cross-core cache line transfer amortizes across in-flight messages,
while ping-pong pays it in full on every exchange.

Latency measurements are markedly more stable than throughput (p50 varies
~5% across runs, throughput ~30%). Throughput variance is dominated by
scheduling and host contention on a VM, not by the ring itself.

### Reading the gap

The two ratios differ by 3x, and the reason is that the socket pays
different costs in each mode.

Under streaming load, the ~200KB socket buffer means the producer writes
many messages before it fills, so neither process ever sleeps. The socket's
cost is two syscalls and two copies per message — roughly 600 ns. That is
the 14x.

Under ping-pong, each exchange drains the buffer, so the reader genuinely
blocks and every message costs a context switch: deschedule, wake on
arrival, wait for the scheduler to run the process. At ~5 µs each way that
dwarfs syscall overhead. That is the 43x.

The tail ratios show the same split. The ring's p99 is 1.5x its p50 —
cache-line transfer time is fairly deterministic. The socket's is 5.1x,
because scheduler wakeup latency is not.

The socket is slower but cheaper: it blocks at 0% CPU while waiting, where
the ring burns a full core spinning. These figures measure latency and
throughput only. On a busy machine, or with more endpoints than cores,
spinning is often the wrong trade.

## The kernel module

`kernel/sysipc.ko` registers a character device at `/dev/sysipc`. Userspace
opens it and calls `mmap`; the module supplies the pages.

- **Page allocation.** `alloc_pages(GFP_KERNEL | __GFP_ZERO, order)` for a
  physically contiguous block, followed by `split_page()` so each page is
  individually refcountable. `__GFP_ZERO` matters: these pages are about to
  be handed to an unprivileged process, and whatever the kernel last stored
  in them would otherwise be readable.
- **Eager population.** `sysipc_mmap` validates the request and then maps
  every page up front with `vm_insert_page`, which takes a reference on each
  page so it cannot be freed while userspace holds it mapped.
- **Validation.** The handler rejects mappings longer than the allocation and
  any nonzero `vm_pgoff`, and sets `VM_DONTEXPAND` so `mremap` cannot grow a
  mapping past the length that was checked. This is enforcement userspace
  cannot bypass — the userspace backend has no equivalent, because no code of
  the author's sits in that path.

### Device vs. userspace backend

| Backend | Mmsg/s (median of 20) |
|---|---|
| `/dev/sysipc` | 24.06 |
| anonymous shared mapping | 23.75 |

Statistically identical, and that is the expected result rather than a
disappointment. Once `mmap` returns, both backends are ordinary loads and
stores against mapped pages. The module is exactly as absent from the data
path as tmpfs was — which is what makes the mechanism zero-syscall in the
first place.

What the kernel backend buys is not speed but capability surface: the
validation above, and the ability to add `poll` and peer-death detection,
neither of which a userspace shared-memory design can offer at any price.

### Finding: compound pages are not individually mappable

`vm_insert_page` failed with `-EINVAL` on page 1 while succeeding on page 0.

`alloc_pages` with order > 0 returns a *compound page*: 2^order contiguous
pages managed as one object, with a single refcount on the head page and
tail pages that point back at it. `vm_insert_page` requires each page to be
independently refcountable, since userspace can map, unmap, or fork them
individually — so it rejects tail pages.

`split_page()` dissolves the compound structure into independently
refcounted pages while preserving physical contiguity. Cleanup then has to
free each page individually rather than freeing the block.

The underlying tension is general: physical contiguity and per-page
refcounting pull in opposite directions, and `split_page` is the explicit
opt-out. Nothing in the function signature indicates this; the errno was
found by instrumenting the insertion loop.

### Finding: `vm_insert_page` is not fault-context safe

The first design populated pages lazily from a `.fault` handler. That hit
`BUG_ON` in `mm/memory.c` — `vm_insert_page` asserts it is not called during
fault handling, where page table locks are already held.

Eager population from `.mmap` is the correct pairing. The cost is giving up
demand paging, which is irrelevant at 128KB but would matter for a
multi-gigabyte region; the fault-safe counterpart is `vmf_insert_page`.

## Finding: padding didn't help, and the reason is interesting

Cache-line padding of `head` and `tail` (`_Alignas(64)`) is the standard
remedy for false sharing in an SPSC queue. Measured under concurrent load
with both processes pinned (`taskset -c 0,1`), it produced no meaningful
improvement:

| Build | Mmsg/s (median of 20) |
|---|---|
| padded | 28.78 |
| unpadded | 27.25 |

That 5% gap is inside the run-to-run spread, which is roughly 30%.

The reason is that this design has *true* sharing, not false sharing. Every
`push` performs an acquire load of `tail`, and every `pop` performs an
acquire load of `head` — each side genuinely reads the other's index on every
message. Separating them onto distinct cache lines doesn't remove the
coherence traffic; it splits one contended line into two.

The fix is to cache the peer index locally and only re-read the shared one on
a suspected full/empty boundary, reducing cross-core reads from once per
message to roughly once per lap. Padding pays off only once that change is in
place, since the two lines are then genuinely independent.

Ping-pong mode showed a 20-25% padding gain, but that mode serializes the two
sides — only one process is ever working — so the contention the padding
targets does not exist there. The concurrent measurement above is the
meaningful one.

_Status: cached-index variant not yet implemented._

## Limitations

This is deliberately a minimal mechanism, not a complete IPC layer:

- **Single producer, single consumer only.** Multiple writers would corrupt
  the indices; MPMC requires CAS-based reservation.
- **Busy-waits.** Both sides spin on full/empty, burning a core. `poll`
  support in the module is the intended fix and is not yet implemented.
- **No peer-death detection.** If one side dies, the other spins forever.
  The module's `.release` handler fires on process death, including abnormal
  termination, which is the hook this would use.
- **Device node is world-accessible.** `devnode` sets mode 0666 for
  development convenience, so any process on the system can map the buffer.
  A real driver would restrict by group or check capabilities in `.open`.
- **Single buffer, single minor.** The module rejects nonzero `vm_pgoff`, so
  there is no way to request a second independent region. Multiple channels
  would use multiple minor numbers.
- **Fixed-size slots.** Messages smaller than `SLOT_SIZE` waste space; a
  short message also leaves the previous message's trailing bytes visible to
  the reader, which is an information leak across a trust boundary.
- **Not truly zero-copy.** Data is copied into and out of the slot. The win
  is eliminating syscalls on the data path, not eliminating copies. A
  reserve/commit API would remove them.
- **Measured on a VM.** Throughput varies ~30% run to run; treat
  single-digit percentage differences as noise. Figures are medians.

## Build and run

Userspace backend:

    cd user
    make                       # or: make CAP=16, make NO_PADDING=1

    ./bench 0                  # throughput
    ./bench 1                  # ping-pong latency
    RUNS=20 ./run_bench.sh     # median/min/max over N runs

    ./producer & ./consumer    # two-process demo over shm_open

Kernel backend:

    cd kernel
    make
    sudo insmod sysipc.ko      # creates /dev/sysipc
    ls -l /dev/sysipc

    cd ../user
    BIN=./bench_dev RUNS=20 ./run_bench.sh
    ./producer_dev & ./consumer_dev

    sudo rmmod sysipc

`make clean` is required between flag changes — make compares timestamps,
not compiler flags. `rmmod` then `insmod` resets the ring, since the module's
buffer persists across process lifetimes and a killed run leaves the indices
mid-stream.

Tested on Ubuntu 24.04, kernel 6.8. `vm_flags_set()` and the single-argument
`class_create()` are 6.3+ and 6.4+ respectively.

## Next

- `poll`/`epoll` support via a wait queue, so a consumer blocks when idle
  instead of spinning — the capability that most clearly distinguishes the
  kernel backend from the userspace one.
- Peer-death detection through the `.release` handler.
- Cached peer indices, after which the cache-line padding above should
  produce a measurable gain.