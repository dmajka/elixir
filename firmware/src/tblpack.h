/* SPDX-License-Identifier: GPL-3.0-only
 * Elixir (22): what a header with packed tables needs (tools/tblpack.py packed_arr). A packed table is a RAM array,
 * filled at boot by tblpack.c tables_unpack from the bytes in flash; its values (NAME_raw) are built only with
 * TBL_RAW (the host tests compare them). The firmware puts the tables in the pool (elixir.c TBL_RAM). */
#pragma once
#ifndef TBL_RAM
#define TBL_RAM
#endif
#ifndef TBL_RAW
#define TBL_RAW 0
#endif
