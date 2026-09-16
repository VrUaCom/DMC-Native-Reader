# PTX Runtime Import Test Matrix v33

Required local/native integration cases:
1. initialize empty/default runtime state;
2. configure reservation;
3. confirmed automatic/default placement case;
4. explicit placement case;
5. no-space/failure case;
6. reservation change resets confirmed manager key/count state;
7. reservation change preserves confirmed pool records/occupancy/payload state;
8. runtime inspection leaves serialized PTX bytes unchanged;
9. ordinary preview path does not initialize runtime compatibility;
10. existing DDS/PTX/PNG/gallery/attachment/render regressions remain behaviorally unchanged.
