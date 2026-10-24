# NR Luna v73 — Android acceptance pass 01

Дата: 2026-09-29  
Репозиторій: `VrUaCom/DMC-Native-Reader`  
Гілка: `NR-Luna-v73`  
APK source commit: `55f874d575203792d86fd88cc49c3209b8db4f14`  
Workflow run: `36587531306`  
APK identity: `android-v72-1.0.45`, ABI `arm64-v8a`

## Scope

Це перший фізичний Android pass для тестової debug-збірки. Цей запис
відділяє фактичну device behavior від статичних і exact-head claims.
У першому фізичному проході код не змінювався. Цей follow-up code pass
містить окрему точкову правку на NR-Luna-v73.

## Passed

- ✅ APK встановився без crash.
- ✅ Відкриття працює з двох доступних маршрутів.
- ✅ Поворот через зовнішній файловий провідник зберігає відкритий ресурс.
- ✅ Базові MOD, SCM, DDS і PTX файли відкриваються.
- ✅ Після відкриття іншого файлу старі ефекти не залишаються.
- ✅ Загальна стабільність у короткому проході нормальна.

## Reproduced regression — session restoration after rotation

### Steps

1. Відкрити `PL000`.
2. Через Native Reader відкрити `EM028`.
3. Повернути екран.

### Expected

Після configuration change залишається поточна сесія
`EM028` разом із вибраним slot/costume.

### Actual

- Після повороту `EM028` зникає.
- Відновлюється попередня сесія `PL000`.
- У пов’язаних сценаріях вибір slot/costume скидається на перший.

### Classification

🔴 `ACCEPTANCE_REGRESSION`: Native Reader відновлює stale/previous
session замість поточної in-app session. Це відрізняється від external
file-manager route, де поточний ресурс зберігається.

Це не виглядає як втрата або помилка парсингу `EM028`; симптом виникає
саме під час Android configuration/state restoration.

## Effect observation — video evidence

Відеозапис переглянуто покадрово. Контекст: `em034.pac · assembled`,
`Lady · costume 1`.

У capture повторюється короткий жовто-білий effect у неправильній точці:

- приблизно `00:07.87` — `S12 · act 5 loop · slot_0006.pac/slot_0005.mot`;
- приблизно `00:13.60` — `S12 · act 4 loop · slot_0006.pac/slot_0004.mot`;
- приблизно `00:15.53` — `S12 · act 3 loop · slot_0006.pac/slot_0003.mot`.

В усіх трьох випадках спалах видно в зоні корпуса/рукоятки зброї, а не на
вільному кінці дула Kalina. Окремо видно короткий rocket-like об'єкт майже у
вертикальній орієнтації, тоді як основна Kalina у кадрі лежить під іншим
кутом. Відео не дає надійно визначити V-resource або траєкторію projectile.

### Preliminary classification

- 🟡 `FX_ATTACHMENT_ORIENTATION_REGRESSION`: position/parent or local-basis
  mismatch is confirmed visually; the code-level owner is now identified.
- Це не виглядає як spawn у світовому origin: effect повторно з'являється
  відносно персонажа/зброї.

## Exact resource and transform trace

The supplied inputs were checked against the pinned v73 implementation and the
canonical executable identity:

- `em034.pac` SHA-256:
  `1a5a245c8348dee3fa17ef1a83da39f15f5c1576e252daf5adc897e349f56dff`;
- `dmc3.exe` SHA-256:
  `e454272ed0fb0247fcbcf300e5d55d7a3e96d50b89b9ffaff81bb978dcbdd082`.

The exact playback chain is:

1. `em034.pac` top-level slot 12 is the CEm034 body MotionScript.
   Its bank 4 actions 3, 4 and 5 play group 4 MOT ids 403, 404 and 405
   from top-level slot 6, and each emits `channel0=1` at script frame 4.
2. The CEm034 action mapping resolves these actions to states
   `0x56`, `0x57` and `0x58`. On lane 1, that signal spawns
   `CEm034Shl02`.
3. The profile binding is `CEm034Shl02 -> V423`, FXBANK slot 28.
   The inner PNST manifest places `V423` at physical record slot 7.
4. The confirmed V423 child graph is:

| child | activation | local translation | local rotation | scale | current presentation |
|---|---:|---|---|---|---|
| E752 | 0 | (60, 0, 0) | (0, 0, 0)° | (1, 1, 1) | presentable sprite |
| E887 | 3 | (60, 0, 0) | (0, 0, 0)° | (1, 1, 1) | retained; non-zero V clock not bridged |
| P337 | 0 | (60, 0, 0) | (0, 90, 0)° | (1, 1, 1) | retained P dependency |

The visible flash in the current portable presentation path is therefore
`E752`, using texture `T5` rectangle `(128,64,64,64)`. The P record is
not rasterized by this path, so the video alone cannot safely assign the
rocket-like silhouette to P337.

The parent chain is also resolved:

- `V423` is owned by dynamic actor `CEm034Shl02`; it is not directly
  attached to a guessed body joint.
- The actor root is built from the current world matrix of Lady component 0,
  model slot 20, local node 0.
- In the active-deployed state used by actions 3/4/5, slot 20 is attached to
  body joint 9 with local translation `(-8.4,-1.0,-1.3)` and
  XYZ rotation `(0,0,3.1415925)` radians.
- Retail constructs the Shl02 actor render basis from the selected matrix
  direction, but the subsequent V423 spawn receives a separate mode-3
  normalized copy of the raw slot20 matrix.
- The visible E752 center therefore belongs to
  `local(T=(60,0,0)) * shl02_effect_parent_matrix(slot20.node0.world)`.
  The actor mesh and the V423 child no longer share a guessed single basis.

### Trace conclusion

The FXBANK identity and V423 local data are not guessed and do not point to a
wrong resource ID. The confirmed mismatch was the reuse of one matrix for two
different retail domains: the actor render basis and the V423 effect parent.
The targeted fix now preserves the actor basis while passing V423 a normalized
raw slot20 parent, matching the retail mode-3 preparation. This is consistent
with the repeated handle/body placement observed in the video, but the
near-vertical rocket-like visual remains unclassified until the separate
P337/E887 presentation and gameplay-world domains are validated on device.

The FXBANK resource mapping remains unchanged. Exact-head build and the next
APK/device pass are still required before calling the effect-placement issue
closed.

## Follow-up implementation pass — Shl02/V423 parent domain

Після першого device pass виконано точкову перевірку canonical dmc3.exe
та внесено правку в NR-Luna-v73 (dd66fe62a47dc9c924ce0b38cb2c81642fc8b92b).

- ✅ Retail callsite 0x14016998e запускає V423 через
  0x1402e7a90 з mode=3, передаючи raw matrix вибраного
  CEm034 slot20 object.
- ✅ Retail 0x1402e7ab0 копіює цю matrix, нормалізує перші три рядки
  через 0x140330390 і зберігає translation row без зміни.
- ✅ Reader тепер розділяє два домени: world динамічного Shl02 залишається
  actor-render basis, а effect_parent_world для V423 бере окрему
  normalized copy raw slot20 matrix.
- ✅ E752 local T=(60,0,0), V423 graph, FXBANK slot 28 та всі resource
  identities не змінювалися.
- ✅ Додано native regression assertion для row-wise normalization і
  збереження translation.
- 🟡 Exact-head build/CTest та APK/device retest ще не виконані в цьому
  follow-up pass.

Це не є доказом фінального pixel-perfect результату до нового APK і
повторення фізичного сценарію S12 act 5/4/3.

## Acceptance disposition

- ✅ Installation: PASS.
- ✅ Baseline opening: PASS.
- 🟡 Effect placement: investigation pending.
- 🔴 Rotation/session/selection restoration: NO-GO.

Перший fix-pass має починатися з persistence поточної Reader session,
resource identity та selected slot/costume під час configuration change.
