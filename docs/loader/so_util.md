# `loader/so_util.c` / `loader/so_util.h` — Documentación de diseño

Comentarios explicativos extraídos del código fuente y reemplazados por bloques Doxygen técnicos en el código. Este documento conserva el razonamiento (el "por qué") separado de la documentación técnica.

## `ku_memcpy` / `ku_flush_caches` (línea ~21)

**Archivo:** `loader/so_util.c` — **Función/estructura:** macros `ku_memcpy`, `ku_flush_caches`

> EMULATOR_BUILD: Vita3K does not implement the kuKernelCpuUnrestrictedMemcpy NID either; under
> the emulator all the memory we write into here is memory we allocated
> ourselves (see the EMULATOR_BUILD path in _so_load), so a plain memcpy works.
>
> Nor does it implement kuKernelFlushCaches. Vita3K's CPU emulation always
> reads fresh memory (no real instruction cache to keep coherent), so this is
> a safe no-op under EMULATOR_BUILD.
>
> Real hardware (`#else`): unprivileged sceKernelAllocMemBlock() cannot create
> executable memory (W^X enforced by the MMU) -- kuKernelAllocMemBlock is
> kubridge's kernel-level allocator that can, and kuKernelCpuUnrestrictedMemcpy/
> kuKernelFlushCaches are needed to write into and sync that memory. Without
> this, the text segment ends up RW-only and any attempt to execute code from
> it faults with a Prefetch Abort exactly at the first instruction fetched.

---

## `_so_load` — arena única bajo EMULATOR_BUILD (línea ~150)

**Archivo:** `loader/so_util.c` — **Función/estructura:** `_so_load`

> Vita3K does not implement kuKernelAllocMemBlock (fixed-address allocation),
> which the code below normally relies on to place the patch/text/data blocks
> at exact, contiguous addresses (mirroring a single mmap of the whole module
> image, like a real ELF loader would do). Since we can't request specific
> addresses under Vita3K, reserve ONE big block up front sized to fit the
> whole image contiguously, and sub-allocate patch/text/data regions from it
> via pointer arithmetic instead of separate fixed-address OS allocations.

---

## `_so_load` — liberación de bloques bajo EMULATOR_BUILD (línea ~335)

**Archivo:** `loader/so_util.c` — **Función/estructura:** `_so_load` (rutas de error `err_free_data`/`err_free_text`)

> patch/text/data_blockid[] all alias the single emu_blockid arena here,
> so only free it once instead of once per alias.

---

## `trampoline_ldm` (línea ~661)

**Archivo:** `loader/so_util.c` — **Función/estructura:** `trampoline_ldm`

> If the register we're reading the offset from is the same as the one we're writing,
> delay it to the very end so that the base pointer isn't clobbered

---

## `so_symbol_fix_ldmia` (línea ~704)

**Archivo:** `loader/so_util.c` — **Función/estructura:** `so_symbol_fix_ldmia`

> This is meant to work around crashes due to unaligned accesses (SIGBUS :/) due to certain
> kernels not having the fault trap enabled, e.g. certain RK3326 Odroid Go Advance clone distros.

---
