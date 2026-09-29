# NR Luna v73 — Android acceptance pass 01

Дата: 2026-09-29  
Репозиторій: `VrUaCom/DMC-Native-Reader`  
Гілка: `NR-Luna-v73`  
APK source commit: `55f874d575203792d86fd88cc49c3209b8db4f14`  
Workflow run: `36587531306`  
APK identity: `android-v72-1.0.45`, ABI `arm64-v8a`

## Scope

Це перший фізичний Android pass для тестової debug-збірки. Цей запис
відділяє фактичну device behavior від статичних і exact-head claims. У цьому
проході код не змінювався.

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
- The final E752 center is composed as
  `local(T=(60,0,0)) * shl02_actor_matrix(slot20.node0.world)`.
  The current actor basis derives direction from slot20 node 0, puts
  `up x direction` in row 0, `direction x row0` in row 1 and direction
  in row 2. Thus the V423 local +X offset is applied along row 0, not along
  the stored direction row.

### Trace conclusion

The FXBANK identity and V423 local data are not guessed and do not point to a
wrong resource ID. The high-confidence code-level suspect is the parent/basis
composition at the Shl02 actor root: the canonical resource supplies a +X
offset, while the current parent matrix maps the motion direction to row 2.
That is consistent with the repeated handle/body placement and the
near-vertical orientation observed in the video.

No code was changed in this trace. The next implementation pass can now be
limited to the Shl02 parent-basis/muzzle alignment and its regression test;
the FXBANK resource mapping must remain unchanged.

## Acceptance disposition

- ✅ Installation: PASS.
- ✅ Baseline opening: PASS.
- 🟡 Effect placement: investigation pending.
- 🔴 Rotation/session/selection restoration: NO-GO.

Перший fix-pass має починатися з persistence поточної Reader session,
resource identity та selected slot/costume під час configuration change.
