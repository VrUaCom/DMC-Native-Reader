# PTX Runtime Import — Phase-2 Authorized Exception v33

The C++23 migration Phase 2 remains compatibility/evidence oriented, with one owner-authorized functional exception: import of the bounded PTX runtime behavior from read-only Rengine commit `50d070e158e484937238d9cb02b2bc6affb2f502` into Native Reader.

This exception is allowed because the imported code itself must become part of the same C++23 baseline and final exact-head evidence set. It does not authorize unrelated modernization or feature expansion.

The exception is gated by a PTX architecture review before implementation and a PTX review gate before the global Review Gate #41.
