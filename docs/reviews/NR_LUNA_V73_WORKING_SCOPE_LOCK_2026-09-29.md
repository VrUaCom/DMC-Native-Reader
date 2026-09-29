# NR Luna v73 — Working Scope Lock

Дата: 2026-09-29

## Єдина дозволена робоча гілка

До повного завершення затвердженого Native Reader plan усі зміни виконуються тільки в:

\`NR-Luna-v73\`

Це стосується:

- source code;
- tests;
- CMake;
- docs;
- runtime/effects;
- UI;
- resource/session pipeline;
- CI preparation;
- APK preparation;
- project status/checklists.

## Заборонені дії до завершення v73 plan

До окремого explicit GO Віктора заборонено торкатися:

- \`main\`;
- \`platform/android\`;
- \`platform/windows\`;
- \`platform/ios\`.

Заборонені:

- merge;
- rebase;
- cherry-pick;
- sync;
- bug-fix;
- CMake/platform correction;
- release/APK promotion;
- force-push;
- deletion або перейменування.

Platform-гілки залишаються збереженими як майбутні wrapper destinations, але не є поточним робочим контуром.

## Порядок

1. Завершити повний plan у \`NR-Luna-v73\`.
2. Закрити всі source/runtime/test/replay/effect blockers.
3. Виконати exact-head CI/APK/physical acceptance для v73.
4. Отримати explicit GO на promotion.
5. Лише після цього окремо синхронізувати Windows, Android та iOS wrappers.

## Branch policy для агентів

Перед кожною дією перевірити target branch. Якщо target не дорівнює \`NR-Luna-v73\`, дію не виконувати.

\`main\` є core baseline, але на цьому етапі він read-only для поточної роботи. Platform branches є deferred wrappers. Поточна canonical implementation line — тільки \`NR-Luna-v73\`.

## Поточна робоча ціль

Довести v73 від source-level integration до повного acceptance:

- generic EffectRuntime;
- повний P/E/G/V update/presentation path;
- actor/effect lifecycle;
- deterministic seek/replay;
- UI presentation toggle;
- current test/evidence contract;
- exact v73 APK;
- physical Android acceptance;
- після цього — promotion і wrapper synchronization.
