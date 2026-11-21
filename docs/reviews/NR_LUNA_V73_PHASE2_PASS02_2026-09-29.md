# NR Luna v73 — Phase 2 Pass 02: evidence-safe P/E/G/V presentation

Дата: 2026-09-29  
Репозиторій: `VrUaCom/DMC-Native-Reader`  
Гілка: `NR-Luna-v73`  
Code checkpoint: `6cae2e1df5906236228a32d051358e71074c72b4`

## Scope lock

- ✅ Зміни виконані тільки в `NR-Luna-v73`.
- ✅ `main`, `platform/android`, `platform/windows`, `platform/ios` не
  змінювалися.
- ✅ Pass 02 продовжує Phase 2; це не перехід до Phase 3.
- ✅ P/G semantic renderer, V-local clock, A animation clock, trajectory,
  collision і grenade lifecycle не вигадуються.

## Що реалізовано

- ✅ Runtime resource gate перевіряє кожен вузол nested P/E/G/V-графа:
  dispatch kind відповідає canonical kind, локальні transform values finite,
  глибина графа обмежена.
- ✅ Presentation вимагає фактичний запис за точним ключем
  `(kind,id,resource_slot)` для кожної child-залежності.
- ✅ P/G залишаються dependency/inspection data; guessed sprite для них не
  створюється.
- ✅ Усі ненульові `V activation_offset` залишаються deferred. Вони не
  перетворюються на MotionScript timer; offset `0` є єдиним безпечним
  presentable entry state.
- ✅ Presentation збирається staged і додається до view лише після повної
  перевірки графа. Відсутня залежність не залишає частково намальований граф.
- ✅ Generic root mapping тепер зберігає точний P/E/G/V dispatch kind.

## Regression coverage

- ✅ Mismatched child dispatch kind відхиляє весь runtime graph.
- ✅ Позитивний і негативний signed V offset зберігаються як deferred metadata.
- ✅ Існуючі Pass 01 перевірки ownership, nested lifetime, resource slot,
  terminal retire і unknown-profile isolation залишені без послаблення.

## Що ще не закрито

- 🟡 Повний V-local update consumer і activation promotion.
- 🟡 A animation progression; поточне E presentation використовує лише
  canonical first frame до доказу окремого A clock.
- 🟡 P/G runtime presentation.
- 🟡 Full deterministic sequential/scrub/reverse replay.
- 🔴 Exact-head Linux/Android build, CMake/CTest execution і v73 APK evidence.
- 🔴 Physical Android effects acceptance та повний grenade/world-collision
  lifecycle.

## Evidence state

- ✅ Code commits: `2918c9a19dd05a85c185d8f707fdeeeb6ca27f2a` і
  `6cae2e1df5906236228a32d051358e71074c72b4`.
- ✅ Branch HEAD перевірений як `6cae2e1df5906236228a32d051358e71074c72b4`.
- 🟡 GitHub combined status для HEAD порожній; workflow run для цього
  checkpoint не є доказом виконання.
- 🟡 Тести додані/розширені, але exact-head compile/CTest у цьому проході не
  запускалися.

## Наступна межа

Наступним bounded slice залишається закриття тільки підтвердженої частини
V update/presentation та її execution validation. P/G renderer, V-local timer,
trajectory і collision semantics переходять далі лише після окремого
EXE/corpus evidence.
