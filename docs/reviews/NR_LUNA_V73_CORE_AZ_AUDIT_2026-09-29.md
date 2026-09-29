# NR Luna v73 — Native Reader Core A→Я Audit

Дата: 2026-09-29  
Репозиторій: `VrUaCom/DMC-Native-Reader`  
Канонічна робоча гілка: `NR-Luna-v73`  
Мета: провести актуальне ядро v73 через увесь Native Reader і не позначати незавершене як виконане.

## Робочий scope lock

- ✅ До завершення v73 plan працюємо тільки в `NR-Luna-v73`.
- ✅ `main`, `platform/android`, `platform/windows`, `platform/ios` read-only для поточного етапу.
- ✅ Platform synchronization, promotion і release branching відкладені до explicit GO після v73 acceptance.
- ✅ Детальне правило: `docs/reviews/NR_LUNA_V73_WORKING_SCOPE_LOCK_2026-09-29.md`.

## Легенда статусів

- ✅ **DONE / EVIDENCED** — виконано й підтверджено кодом, тестом або зафіксованим ref.
- 🟡 **IN PROGRESS / PARTIAL** — частково виконано або потребує наступного проходу.
- 🔴 **BLOCKED / NO-GO** — є реальний блокер; завершення не заявляється.
- 🔵 **PLANNED** — ще не починалося.
- ⚫ **REJECTED / OUT OF SCOPE** — свідомо не переноситься або попередня модель відхилена.

## 0. Exact refs і межі аудиту

- ✅ `main`: `52210874f74d5526c2f1bd9000aaeab4594a03de`
- ✅ `NR-Luna-v73`: `7b34490a53c7d221ca94cc5c5925b3ee74e2030c`
- ✅ v73 випереджає `main` на 8 комітів і не відстає від нього.
- ✅ Phase 2 historical snapshot: `phase2/consolidated` = `e8f380113b7372210745957befcd8f890b9caa7a`.
- ✅ Phase 2 вже входить в ancestry `main`; повторне blind merge не потрібне.
- 🟡 Phase 3 historical snapshot: `phase3/consolidated-pre-lady` = `d0154abca364f9fb1194e72770fc3de760b9a4b9`; він diverged від current main і має 39 унікальних старих комітів. Потрібна selective reconciliation.
- ✅ `V83` не є окремою GitHub-гілкою; актуальна лінія — `NR-Luna-v73`.

## A. Core identity і архітектурна межа

- ✅ Один Native Reader core у `main`/v73.
- ✅ Native Reader core використовує target-scoped C++23.
- ✅ Rengine ReaderCore залишається окремою C++20 dependency.
- ✅ CMake має duplicate-source guard.
- ✅ CMake має duplicate-native-test guard.
- ✅ Lady/FX changes не створюють окремого другого ядра.
- 🟡 Потрібно завершити формальне перенесення всіх актуальних branch-specific core changes у `main`, коли v73 пройде acceptance.
- ⚫ General Rengine mutation не дозволяється; Rengine використовується як read-side/reverse authority.

## B. Phase 1

- ✅ Phase 1 research/checkpoint присутній у program ancestry.
- ✅ Архітектурна межа C++23-first Native Reader зафіксована.
- ✅ Правило єдиної semantic authority зафіксоване.
- ✅ Reader/Rengine ownership boundary зафіксована.
- ✅ Phase 1 не треба повторно переносити як окрему гілку.
- 🟡 Потрібно позначити Phase 1 як historical completed baseline у project-картці.

## C. Phase 2 — C++23 baseline і evidence contract

- ✅ Phase 2 source/static work інтегрований у `main` і є базою v73.
- ✅ Exception/noexcept boundary зафіксована.
- ✅ PTX RuntimeCompat architecture/import history збережена.
- ✅ FXBANK/Visual-Info/post-v68 additions уже присутні в current main ancestry.
- ✅ v73 runner inventory адаптований з historical 23 до фактичних 28 CMake tests.
- ✅ `main` runner inventory адаптований до фактичних 24 tests.
- ✅ Historical 23-test Phase 2 contract не переписується.
- 🔴 Exact-head v73 Linux/Android execution ще не підтверджена.
- 🔴 Debug APK і unsigned release APK саме з v73 ще не зібрані та не перевірені.
- 🔴 Physical Android evidence саме для v73 ще не пройдена.
- 🟡 Старі issue cards Phase 2 містять застаріле формулювання про PR #95 і потребують reconciliation.

## D. Format і resource materialization

- ✅ PAC/PNST resource session pipeline використовується як спільна основа.
- ✅ FXBANK canonical identity = `(kind, u16 id)`.
- ✅ FXBANK loader ABI виправлений під EXE.
- ✅ M record споживає record + companion slot.
- ✅ Companion може бути PTX або empty slot; правило `0x31` відкинуто.
- ✅ Exact effect resource provenance зберігається: kind, id, slot.
- ✅ Resource dependencies не зливаються в один guessed resource.
- ✅ Environment collision structures додані до v73 reader model.
- 🟡 Повна room collision execution semantics ще не доведена.
- 🟡 Runtime collision/target-world manager для standalone Reader залишається зовнішньою dependency.
- ⚫ Unknown resource не підміняється guessed EFM/FX моделлю.

## E. MotionScript і runtime action execution

- ✅ Raw MOT і Script Play розділені.
- ✅ Raw MOT не повинен запускати script effects, actors або signals.
- ✅ Script Play проходить через controller/bank/action/MOT/lane runtime.
- ✅ Lane isolation присутня.
- ✅ CEm034 state/signal bridge присутній.
- ✅ Dynamic actor events відокремлені від persistent equipment.
- ✅ Seek/reset базова runtime модель присутня.
- 🟡 Full replay determinism через реальний `run_script_frame` і всі actor/effect consumers ще не доведена.
- 🟡 Reverse seek 45 → 10 і sequential-vs-scrub acceptance ще не green.
- ⚫ Lady-specific `play_lady_effect()` API не допускається.

## F. Generic EffectRuntime

### Уже підтверджено

- ✅ Generic `EffectRuntime`.
- ✅ `ScriptEffectBridge`.
- ✅ `RuntimeEffectSpawn`.
- ✅ `RuntimeEffectInstance`.
- ✅ `RuntimeEffectEvent`.
- ✅ Parent domains: World, CharacterRoot, BodyJoint, PersistentComponent, DynamicActor, RuntimeMatrix, ProjectileTransform, TetherPoint.
- ✅ Evidence gate.
- ✅ Resource gate.
- ✅ Deferred transform handling.
- ✅ Presentation-only visibility toggle model.
- ✅ No joint9 fallback.
- ✅ Actor-owned lifetime model.
- ✅ Canonical bindings:
  - ✅ Shl00 → V463.
  - ✅ Shl02 → V423.
  - ✅ Shl05 → V276.
  - ✅ Shl04 → V488.
  - ✅ Shl04 stored pointer → V475.
- ✅ V473 correctly rejected from MotionScript path because it belongs to damage/collision.
- ✅ V435/V277 not guessed standalone because they require collision/world conditions.
- ✅ No guessed muzzle flash/explosion/smoke naming.

### Що ще не закрито

- ✅ Generic profile registry доданий у `effect_profile_registry.cpp`; Lady binding data винесена з shared `part_attachment.cpp`; unknown profiles залишаються effect-free.
- 🔴 P/G/V runtime update/presentation не завершені:
  - E children materialize only partially;
  - P/G children retained but not rendered;
  - V activation offsets are not fully advanced by a bridged local clock;
  - animated E currently uses only the first atlas frame.
- 🔴 Повний grenade/trajectory/impact/explosion-like lifecycle не підтверджений на Android.
- ✅ Deep-copy/lifetime contract для nested EffectBinding child spans закритий у Pass 01: Session і EffectRuntime володіють deep-copied child graph.
- 🟡 Full child graph update/retire tests ще не green.

## G. Dynamic actors, stage і collision

- ✅ Shl00..05 lifecycle reverse data збережена.
- ✅ Shl02 exact spawn pose canonical.
- ✅ Shl02 gameplay steering не фальсифікується.
- ✅ Shl03 → slot26 + slot30 canonical path.
- ✅ slot30: 5 bones, 82 vertices, 131 influences.
- ✅ slot30 points: 0%, 25%, 50%, 75%, 100%.
- ✅ slot30 uses canonical skin palette/MOD skinning.
- ✅ Temporal smoothing hypothesis rejected.
- 🟡 Shl02 post-spawn target/collision behavior requires gameplay-world context.
- 🟡 Canonical room collision resource can be inspected, але повний gameplay target manager не належить standalone Reader.
- ⚫ Fake Shl02 straight-line trajectory не використовується.

## H. Equipment, costume parity і component state

- ✅ Persistent components 0..4 → slots 20..24.
- ✅ BodyStowed/ActiveDeployed placement records.
- ✅ Component3 body-root special case.
- ✅ Action46 scale 1.0 ↔ 1.5.
- ✅ Action50 corrected channel/state transitions.
- ✅ Component0..4 canonical placement/control-domain split.
- ✅ Costume1 і costume2 використовують одну runtime logic.
- 🟡 Costume2 physical Android acceptance ще потрібно повторити на v73 APK.
- ⚫ Old slots20..26/30 → joint9 model rejected.

## I. UI і inspection

- ✅ Raw MOT button окремий.
- ✅ Script Play buttons з’являються тільки для реально linked scripts.
- ✅ Inactive/unlinked Script buttons ховаються.
- ✅ MOT strip не засмічується зайвими кнопками.
- ✅ Visual/Info dual-preview flow збережений.
- 🟡 Runtime Effects ON/OFF як Android presentation toggle ще не виведений через JNI/MainActivity.
- 🔵 Optional debug overlay: State, Script, Bank, Action, Lane, Effect events.
- 🔵 Contextual reset/two-mode effect viewer redesign потребує окремого UI pass.

## J. Platform wrappers

- ✅ Канонічні wrapper-гілки визначені:
  - `platform/android`
  - `platform/windows`
  - `platform/ios`
- ✅ Android wrapper зберігає platform-specific diagnostics/UI.
- ✅ Windows wrapper зберігає Windows-specific parity/Open With.
- ✅ iOS wrapper зберігається окремо від portable core.
- 🟡 Android branch: 31 власний commit, відстає від current main на 1.
- 🟡 Windows branch: 7 власних commits, відстає від current main на 1.
- 🟡 iOS branch: 94 власні commits, відстає від current main на 1.
- 🔵 Потрібно інтегрувати current main core у кожну wrapper-гілку без втрати wrapper changes.
- ⚫ Platform wrappers не повинні містити власні копії semantic core.

## K. Tests і CMake

- ✅ v73 CMake inventory = 28 registered native tests.
- ✅ v73 `run_phase2_exact_head.py` expected inventory = 28.
- ✅ `effect_runtime` test присутній.
- ✅ `effect_visualization` test присутній у v73 CMake.
- ✅ Lane isolation/resource/evidence/deferred transform/reset базові тести присутні.
- 🟡 Full exact-head v73 test execution не підтверджена в доступному середовищі.
- 🟡 Full V/P/G renderer tests відсутні.
- 🟡 Full effect replay sequential/scrub/reverse tests відсутні.
- 🟡 Android JNI Effects ON/OFF test відсутній.
- 🟡 Dynamic actor/effect cleanup regression matrix неповна.
- 🔵 Costume1/costume2 physical regression script потребує formalization.

## L. CI, APK і physical acceptance

- 🔴 Exact-head v73 GitHub Actions run із canonical SDK/NDK/JDK ще не підтверджений.
- 🔴 v73 debug APK не має нового accepted SHA.
- 🔴 v73 unsigned release APK не має нового accepted SHA.
- 🔴 APK verifier evidence для v73 не зафіксована.
- 🔴 Samsung installed footprint evidence не зафіксована.
- 🔴 Physical effects acceptance не пройдена.
- 🔴 Grenade full chain: spawn → trajectory → collision/bounce → detonation/retire не пройдений.
- 🟡 Базовий em034 costume1 assembly і Script Play уже підтверджені старими physical runs.
- 🟡 Costume2, Shl02, Shl03, slot30 tether потребують повторного v73 acceptance.

## M. Project cards і branch governance

- ✅ Цільові 5 активних гілок визначені.
- ✅ Старі branches не видалялися.
- ✅ Phase 1/2 історія не втрачена.
- 🟡 78 branch refs ще присутні в репозиторії.
- 🟡 Archive tags і branch cleanup не є поточною v73 implementation роботою.
- 🟡 До завершення v73 жодна історична гілка не видаляється.
- 🔵 Після v73 acceptance можна створити immutable archive tags і виконати окремий cleanup pass.
- 🔵 Після explicit GO потрібно захистити п’ять canonical branches.

## Закриття v73

v73 не можна позначити як повністю завершений, доки не закриті всі 🔴 блокери:

1. exact-head CI/build з поточним v73.
2. повний P/E/G/V renderer/update path.
3. deterministic effect replay і reverse seek.
4. Android Effects ON/OFF presentation bridge.
5. physical Android effects acceptance.
6. grenade/world collision lifecycle.
7. generic runtime, tests і v73 physical acceptance.

Platform synchronization та branch cleanup є **deferred post-v73 operations** і не виконуються в поточному scope.

## Рекомендований порядок виконання

1. ✅ Generic profile registry і винесення Lady binding data з shared hardcoded path — `27f192a`.
2. 🟡 V/P/G/E animation/update/presentation graph.
3. Full effect lifecycle + cleanup tests.
4. Sequential/scrub/reverse replay tests.
5. JNI/MainActivity Effects ON/OFF.
6. Exact-head CI/APK.
7. Costume1/costume2 physical acceptance.
8. Wrapper branch synchronization.
9. Archive tags і branch cleanup.
10. Після цього — selective promotion стабільних v73 core changes у `main`.
