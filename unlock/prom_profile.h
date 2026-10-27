#ifndef GU_PROM_PROFILE_H
#define GU_PROM_PROFILE_H
/* AD106 RUN000: BAR0+0x300000 begins at AD106.rom+0x9400.
 * PROM[0,0x9a200) equals the board ROM chain; remaining bytes are FF.
 * Share this mapping between preflight verification and FWSEC extraction. */
#define GU_PROM_BAR_OFFSET 0x300000U
#define GU_PROM_VIEW_BYTES 0x9a200U
#define GU_PROM_VIEW_SHA256 "45b833dbf75f97e1f3ae7d1eee37d0609d351715e010b84690c1bb0ba1805b48"
#endif
