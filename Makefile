# Invoked only through the root Makefile (make/ps2.mk).
# The standard IOP rules supply imports.o and allow a module with no exports.
IOP_BIN = vmc.irx
IOP_OBJS = vmc.o vmc_format.o imports.o

.PHONY: all clean
all: $(IOP_BIN)

clean:
	rm -f $(IOP_BIN) $(IOP_BIN:.irx=.notiopmod.elf) $(IOP_BIN:.irx=.notiopmod.stripped.elf)
	rm -rf obj

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal

$(IOP_OBJS_DIR)vmc.o: vmc_rpc.h vmc_format.h irx_imports.h
$(IOP_OBJS_DIR)vmc_format.o: vmc_format.h irx_imports.h
