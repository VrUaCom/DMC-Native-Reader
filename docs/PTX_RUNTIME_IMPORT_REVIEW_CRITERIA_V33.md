# PTX Runtime Import Review Criteria v33

The review must answer:
1. Is the import strictly Reader-only with Rengine untouched?
2. Is only the authorized PTX runtime slice copied?
3. Does the existing serialized PTX parser remain the only disk-format authority?
4. Is the copied code adapted to Native Reader C++23 and exception contracts?
5. Is runtime activation lazy?
6. Are unknown palette/finalizer/cleanup stages still explicit unknowns?
7. Are tests integrated into the existing Core test graph?
8. Are package/installed-size and duplicate constraints still satisfiable?
9. Does Spider remain orchestration rather than parser/runtime authority?
10. Is there one exact HEAD suitable for the global Phase-2 review?
