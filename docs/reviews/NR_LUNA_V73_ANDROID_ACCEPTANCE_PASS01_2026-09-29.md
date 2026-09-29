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

## Effect observation

Під час запуску MotionScript спостерігалися:

- короткий effect біля рукоятки замість дула;
- коротка rocket-like visual у вертикальній орієнтації.

Без точного action ID або відео не можна достовірно визначити, чи це
неправильний parent/joint, local rotation або конкретний V-resource. Потрібен
окремий reproduction capture.

## Acceptance disposition

- ✅ Installation: PASS.
- ✅ Baseline opening: PASS.
- 🟡 Effect placement: investigation pending.
- 🔴 Rotation/session/selection restoration: NO-GO.

Перший fix-pass має починатися з persistence поточної Reader session,
resource identity та selected slot/costume під час configuration change.
