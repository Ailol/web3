
# WEB3.qck

> **ALL STAYS AS IS — SO THAT YOU GET TO KEEP YOUR PREFERENCES. IT JUST UPGRADES IT.**

`WEB3.qck` is a portable spatial-computation layer for the web and development space.

It does **not** ask languages, frameworks, browsers, APIs, IDEs, databases, or operating systems to move first.

C stays C. Java stays Java. .NET stays .NET. JavaScript stays JavaScript. Existing REST APIs stay REST APIs. Existing tools remain usable.

The upgrade is a shared layer of **rasterized space, bitmaps, vectors, depth, portable execution, persistent datasets, and AI exploration**.

```
existing web / apps / tools / APIs
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

**No giant migration. No required rewrite. No company has to move first.**

A system can be upgraded incrementally:

```
0% rewrite
+ one probe
+ one projection
+ one dataset
+ one portable runtime
```

Everything else can remain exactly where it already works.

## Core primitives

### Bitmap

What exists, changed, matters, is occupied, is visible, is selected, or is allowed.

```
changed
failed
blocked
requested
uncertain
relevant
```

Bitmaps make state composable and cheap to query.

### Rasterization

Screens, pages, dashboards, terminals, traces, cameras, and other visual surfaces can become spatial information instead of requiring direct injection into every system.

```
frame
  ↓
regions / boxes / depth
  ↓
attention bitmap
  ↓
*explore
```

### Vector

Bitmaps say **what/where**. Vectors say **how to move through it**.

```
bitmap = occupancy / salience
vector = direction / movement
 depth = structure
```

Together they give AI and humans a learnable spatial representation.

### Depth

A flat surface can be projected into structural depth without replacing the original application.

```
rendered surface
      ↓
component
      ↓
parent
      ↓
source / state / events / network
```

### Portable execution

WebAssembly provides a browser-side execution seam; C/native code provides the host-side seam.

```
browser  ↔ WASM
native   ↔ C
state    ↔ bitmap / dataset
```

The public model should stay small enough to travel between environments.

## AI exploration

The goal is not merely to make AI "look" at software.

The goal is to give AI a **space to explore**.

```
./  = locality / start here
*   = explore / keep the path live
&   = resolved address
```

A visual or runtime change can seed exploration:

```
attention
   ↓
*object
   ↓
*relations
*depth
*state
*history
   ↓
&resolved context
```

This allows AI to stay oriented without every tool requiring a bespoke direct integration first.

## qck.dev + qck.nit

`qck.dev` is the remembered computational development world.

`qck.nit` resolves visual surfaces into depth and gives the space a geometry that humans and AI can explore.

```
qck.dev
   │
   ├─ state
   ├─ data
   ├─ units
   ├─ attention
   └─ projections
         │
         └─ qck.nit
              ↓
       raster / bitmap / vector / depth
```

The projection may be windowed, borderless, or fullscreen while the host operating system continues providing drivers, scheduling, GPU, input, and filesystem services.

## Compatibility first

WEB3.qck is not a replacement requirement.

```
C          ✓
C++        ✓
Java       ✓
.NET       ✓
JavaScript ✓
TypeScript ✓
Python     ✓
Rust       ✓
REST       ✓
SQL        ✓
Mongo      ✓
Redis      ✓
Browsers   ✓
IDEs       ✓
```

Adapters and projections should attach to what already exists rather than forcing an ecosystem rewrite.

## First build target

Keep the first implementation tiny:

```
1. raster / geometry input
2. stable object IDs
3. bitmap planes
4. vector movement
5. depth projection
6. attention
7. C + WASM execution seam
8. persistent portable dataset
```

Then optimize the hot path with packed structs, delta updates, GPU instancing, SIMD, and BitMemory where profiling shows it matters.

---

## WEB3.qck

**The web does not have to be replaced to become spatial, portable, and AI-explorable.**

Everything stays as is.

It just upgrades it.
