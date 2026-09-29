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

- 🟡 `FX_ATTACHMENT_ORIENTATION_REGRESSION`: неправильні position/parent або
  local rotation підтверджені візуально; code-level owner ще не визначений.
- Це не виглядає як spawn у світовому origin: effect повторно з'являється
  відносно персонажа/зброї.
- Точний V-resource, parent/joint і числовий local transform залишаються
  відкритими для code-level trace.

## Acceptance disposition

- ✅ Installation: PASS.
- ✅ Baseline opening: PASS.
- 🟡 Effect placement: investigation pending.
- 🔴 Rotation/session/selection restoration: NO-GO.

Перший fix-pass має починатися з persistence поточної Reader session,
resource identity та selected slot/costume під час configuration change.
