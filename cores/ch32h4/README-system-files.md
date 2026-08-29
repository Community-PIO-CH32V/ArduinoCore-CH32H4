# Why there is only one system file

`system_ch32h417_v3f.c` serves **both** cores.

WCH ships a `system_ch32h417_v3f.c` and a `system_ch32h417_v5f.c`, and both
define `SystemClock`, `HCLKClock` and `SystemCoreClock`. In a single ELF that
is three multiple-definition errors, which is the same collision that made both
prior ports to this silicon ship two ELFs.

It resolves to nothing, because the two files' `SystemAndCoreClockUpdate()`
implementations are **byte-identical once whitespace is normalised**, and the
function is already core-aware:

```c
if (NVIC_GetCurrentCoreID() == 0)  SystemCoreClock = HCLKClock;  /* V3F */
else                               SystemCoreClock = tmp3;       /* V5F */
```

So the V5F copy contributes nothing the V3F copy does not, and it was deleted.
The V3F copy additionally carries `SystemInit()`, which programs every PLL and
which only the V3F may call -- reconfiguring PLLs underneath a running core is
exactly as bad as it sounds. The V5F calls `SystemAndCoreClockUpdate()` alone.

If a future vendor drop makes the two implementations diverge, this stops being
true and the update function has to be split by core.
