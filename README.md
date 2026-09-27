# vmc

A small PS2 IOP module: the RPC front end for a virtual memory card.

The EE loads ps2sdk's `vmcman.irx` (mcman's PS2 memory card filesystem over
an iomanX image file instead of SIO2), then `vmc.irx`. Over its own SIF RPC
(`vmc_rpc.h`, SID `VMC1`) this module:

- mounts `vmc0:` / `vmc1:` on formatted card images (`PS2_VMC_MOUNT`, one
  per slot); under ps2link an image can live on the PC as `host:<path>`;
- runs plain iomanX file operations on those paths: open, read, write,
  close, mkdir, with data moved in 4 KB chunks;
- remounts a slot (`PS2_VMC_REMOUNT`) after the host session that owned its
  file handle went away.
- creates a missing image as a new formatted 8 MB card (`PS2_VMC_FORMAT`,
  `vmc_format.c`: the same bytes as HyperSolar's `tools/vmc/ps2mc.py format`
  apart from the root timestamps).

Images use the PCSX2 layout (`Mcd00N.ps2`, 528-byte pages with ECC), so the
same file opens in PCSX2. `vmcman` needs the fixes in `ninjadynamics/ps2sdk`
(`iop/memorycard/mcman/src/mciomanx_backing.c`); upstream's variant cannot
mount a standard 8 MB image.

Built by HyperSolar's root Makefile (`make ps2-hw PS2_VMC1=... PS2_VMC2=...`); the
Makefile here uses the standard ps2sdk IOP sample rules.
