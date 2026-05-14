# SPDX-License-Identifier: GPL-2.0-only

# Workaround: modern Clang generates jump tables with relocations
# that GNU ld 2.34 considers "unsupported transfer". Force no jump tables.
CFLAGS_gsi.o += -fno-jump-tables

gsidbg-$(CONFIG_GSI) += gsi_dbg.o
obj-$(CONFIG_GSI) += gsi.o gsidbg.o

obj-$(CONFIG_IPA_EMULATION) += gsi_emulation.o
