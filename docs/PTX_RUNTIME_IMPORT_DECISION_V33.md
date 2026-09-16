# PTX Runtime Import Authorization — v33

Owner decision, 2026-09-16:

- `VrUaCom/dmc-rengine-cpp` remains absolute READ-ONLY.
- Native Reader is authorized to read the reverse-recovered PTX implementation at commit `50d070e158e484937238d9cb02b2bc6affb2f502`.
- The minimum required PTX runtime code may be copied into `VrUaCom/DMC-Native-Reader`.
- The copied code must be adapted/ported to the Native Reader ISO C++23 architecture and wrapped behind the Reader-owned PTX runtime compatibility boundary.
- All further implementation/maintenance of the copied slice occurs only in DMC Native Reader.
- This is a PTX-specific exception. It does not authorize general copying of other Rengine subsystems.
- Existing serialized PTX framing/TextureSet/DDS behavior remains the file-format authority; imported reverse code represents runtime behavior, not a second disk parser.
- Project plans and review gates must explicitly track the import and re-evaluate the next stages after each review.
