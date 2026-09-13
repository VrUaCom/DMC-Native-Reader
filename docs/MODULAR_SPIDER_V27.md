# Native Reader v27 — historical architecture note

This document has been superseded by [`MODULAR_SPIDER_V33.md`](./MODULAR_SPIDER_V33.md).

The v27 document originally described the four-family MOD / SCM / DDS / PTX stage of the architecture. It is no longer authoritative because the current v33 line also promotes EventTbl, routes composition/PTX product actions through Spider Crusader, uses one-decode shared PTX banks, pins a newer canonical DMC Rengine ReaderCore, and enforces the single-DSO/16 KiB Android packaging contract.

Use the v33 document for current implementation and acceptance rules. Git history remains the authority for the historical v27 design.
