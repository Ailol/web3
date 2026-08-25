# OS3.qck

> **ALL STAYS AS IS — SO THAT YOU GET TO KEEP YOUR PREFERENCES. IT JUST UPGRADES IT.**

`OS3.qck` lifts the [WEB3.qck](README.md) idea one layer down: from the page to the **whole machine**.

Where `WEB3.qck` makes the web spatial, portable, and AI-explorable, `OS3.qck` does the same for the operating system — **without replacing it**.

Windows stays Windows. Linux stays Linux. macOS stays macOS. Your kernel, drivers, scheduler, filesystem, and GPU stack all stay exactly where they already work.

The upgrade is a shared layer of **rasterized space** over what the OS already exposes: windows, processes, files, devices, sockets, and threads.

```
existing OS / windows / processes / files / devices
              │
              │ pollen / observe / project
              ▼
        rasterized space
              │
      ┌───────┼────────┐
      ▼       ▼        ▼
   bitmap   vector    depth
      │       │        │
      └───────┼────────┘
              ▼
        portable state
              │
      C / WASM / native
              │
              ▼
      human + AI explore
```

## The rule

**No new kernel. No required reboot into something else. No driver rewrite. No company has to move first.**

A machine can be upgraded incrementally:

```
0% OS rewrite
+ one probe
+ one projection
+ one dataset
+ one portable runtime
```

Everything else keeps running natively.

## What becomes spatial

The OS already tracks everything as identifiers. `OS3.qck` gives those identifiers a **place**.

```
window   → box   (x, y, w, h, z, parent)
process  → node  (pid, parent, threads)
file     → cell  (path, size, mtime, flags)
device   → port  (in / out / capacity)
socket   → edge  (src ↔ dst, throughput)
```

Each gets a stable ID, a bitmap plane, and a depth relation — the same primitives as `qck.nit`.

## Core primitives

### Bitmap

What is running, focused, changed, blocked, allowed, or leaking.

```
running
focused
changed
blocked
allowed
leaking
```

One bit per resource makes machine state composable and cheap to query.

### Rasterization

The desktop, the window tree, `top`/Task Manager, the filesystem, and the process graph all become spatial surfaces instead of one-off APIs.

```
machine frame
  ↓
regions / windows / processes / mounts
  ↓
attention bitmap
  ↓
*explore
```

### Vector

Bitmaps say **what/where**. Vectors say **how state is moving**.

```
bitmap = occupancy / load
vector = flow (CPU, IO, net, memory)
 depth = ownership / parentage
```

### Depth

A flat resource list projects into structural depth without replacing the OS.

```
window
   ↓
process
   ↓
parent process
   ↓
user / session / cgroup / namespace
```

### Portable execution

WebAssembly is the sandboxed seam; C/native is the host seam. The OS keeps providing drivers, scheduling, GPU, input, and filesystem.

```
sandbox  ↔ WASM
native   ↔ C
state    ↔ bitmap / dataset
```

The public model stays small enough to travel between machines.

## AI exploration

The goal is not to make AI "watch" your computer.

The goal is to give AI a **machine-shaped space to explore** — safely, read-first.

```
./  = this machine / start here
*   = explore / keep the path live
&   = resolved handle
```

A resource change can seed exploration:

```
attention (spike / new window / new process)
   ↓
*resource
   ↓
*owner
*children
*handles
*history
   ↓
&resolved context
```

## qck.dev + qck.nit + qck.os

```
qck.dev
   │
   ├─ state
   ├─ data
   ├─ units
   ├─ attention
   └─ projections
         │
         ├─ qck.nit  → the page as space
         └─ qck.os   → the machine as space
                ↓
         raster / bitmap / vector / depth
```

The projection may be windowed, borderless, or fullscreen while the host OS keeps full control of the hardware.

## Compatibility first

`OS3.qck` is not a replacement requirement.

```
Windows    ✓
Linux      ✓
macOS      ✓
WSL        ✓
containers ✓
VMs        ✓
kernels    ✓ (unchanged)
drivers    ✓ (unchanged)
filesystems✓
schedulers ✓
GPU stacks ✓
```

Adapters and projections attach to what the OS already exposes rather than forcing a platform rewrite.

## First build target

Keep the first implementation tiny — same shape as `qck.nit`, new source of boxes:

```
1. resource enumeration (windows / processes / files)
2. stable object IDs
3. bitmap planes (running / focused / changed)
4. vector flow (cpu / io / net)
5. depth projection (ownership tree)
6. attention (spikes / new / gone)
7. C + WASM execution seam
8. persistent portable dataset
```

Then optimize the hot path with packed structs, delta updates, ring buffers, SIMD, and BitMemory where profiling shows it matters.

---

## OS3.qck

**The operating system does not have to be replaced to become spatial, portable, and AI-explorable.**

Everything stays as is.

It just upgrades it.
