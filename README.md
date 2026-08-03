# sysipc

A lock-free single-producer/single-consumer ring buffer for inter-process
communication over shared memory, written in C11.

Two unrelated processes map the same physical pages and exchange fixed-size
messages with no syscalls on the data path. Synchronization lives entirely
in the shared region as atomic head/tail indices with acquire/release
ordering — no locks, no kernel involvement after setup.

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

This is also why the ring spins rather than blocks: it trades a burned core
for avoiding the wakeup entirely. A kernel implementation with `poll`
support could block when idle and keep the fast path when busy, rather than
choosing one.

## Finding: padding didn't help, and the reason is interesting

Cache-line padding of `head` and `tail` (`_Alignas(64)`) is the standard
remedy for false sharing in an SPSC queue. Measured under concurrent load,
it produced **no improvement**:

| Build | Mmsg/s (median of 20) |
|---|---|
| padded | 23.3 |
| unpadded | 27.3 |

That difference is inside the run-to-run noise.

The reason is that this design has *true* sharing, not false sharing. Every
`push` performs an acquire load of `tail`, and every `pop` performs an
acquire load of `head` — each side genuinely reads the other's index on every
message. Separating them onto distinct cache lines doesn't remove the
coherence traffic; it splits one contended line into two.

The fix is to cache the peer index locally and only re-read the shared one on
a suspected full/empty boundary, reducing cross-core reads from once per
message to roughly once per lap. Padding pays off only once that change is in
place, since the two lines are then genuinely independent.

_Status: cached-index variant not yet implemented._

## Limitations

This is deliberately a minimal mechanism, not a complete IPC layer:

- **Single producer, single consumer only.** Multiple writers would corrupt
  the indices; MPMC requires CAS-based reservation.
- **Busy-waits.** Both sides spin on full/empty, burning a core. There is no
  way to block without kernel support.
- **No peer-death detection.** If one side dies, the other spins forever.
- **No access control.** Any process able to open the shared object has full
  read/write access to the region.
- **Fixed-size slots.** Messages smaller than `SLOT_SIZE` waste space; a
  short message also leaves the previous message's trailing bytes visible to
  the reader, which is an information leak across a trust boundary.
- **Not truly zero-copy.** Data is copied into and out of the slot. The win
  is eliminating syscalls on the data path, not eliminating copies. A
  reserve/commit API would remove them.

## Build and run

    cd user
    make                      # or: make CAP=16, make NO_PADDING=1

    ./bench 0                 # throughput
    ./bench 1                 # ping-pong latency
    RUNS=20 ./run_bench.sh    # median/min/max over N runs

    ./producer & ./consumer   # standalone two-process demo over shm_open

`make clean` is required between flag changes — make compares timestamps,
not compiler flags.

## Next

A Linux kernel module exposing the same mechanism through a character
device, which addresses several limitations above: kernel-enforced access
control, `poll`/`epoll` support so consumers block instead of spinning, and
cleanup on process death via the driver's `release` callback.