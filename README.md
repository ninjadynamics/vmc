# vmc

A small PS2 IOP module: the RPC front end for a virtual memory card.

The EE loads ps2sdk's `vmcman.irx` (mcman's PS2 memory card filesystem over
an iomanX image file instead of SIO2), then `vmc.irx`. Over its own SIF RPC
(`vmc_rpc.h`, SID `VMC1`) this module:

- mounts `vmc0:` on a card image (`PS2_VMC_MOUNT`); under ps2link the image
  can live on the PC as `host:<path>`;
- runs plain iomanX file operations on `vmc0:` paths: open, read, write,
  close, mkdir, with data moved in 4 KB chunks.

Images use the PCSX2 layout (`Mcd00N.ps2`, 528-byte pages with ECC), so the
same file opens in PCSX2. `vmcman` needs the mount size fix from
`ninjadynamics/ps2sdk` (`iop/memorycard/mcman/src/mciomanx_backing.c`);
upstream's check rejects every standard 8 MB image.

Built by HyperSolar's root Makefile (`make ps2-hw PS2_VMC=<image>`); the
Makefile here uses the standard ps2sdk IOP sample rules.
