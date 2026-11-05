# NR Luna v73 — Pass 01: Binding Graph Ownership

Дата: 2026-09-29  
Репозиторій: `VrUaCom/DMC-Native-Reader`  
Канонічна робоча гілка: `NR-Luna-v73`  
Parent HEAD: `29b26adf4b5bf2c2ee30328fb30209757baa098b`
Implementation checkpoints: `9e8c492` → `03a85c9` → `b9d82ea` → `2045e8c`

## Scope

- ✅ Змінюється тільки `NR-Luna-v73`.
- ✅ `main`, `platform/android`, `platform/windows`, `platform/ios` не зачіпаються.
- ✅ Не додається жодна guessed P/G/V семантика, таймер або trajectory.

## Pass 01 — статус

- ✅ Generic EffectRuntime копіює весь nested `EffectChildRef` graph у runtime-owned storage.
- ✅ Session registration boundary копіює child graph до завершення lifetime profile provider і зберігає Session copyability.
- ✅ Self-replacement/reconfiguration зберігає старі span owners до завершення копіювання.
- ✅ Resource gate продовжує перевіряти root і всі nested child resources з exact `(kind,id,slot)`.
- ✅ Додані regression assertions для runtime-level і Session-level lifetime safety.
- ✅ Canonical retire є terminal; пізніший update не resurrect-ить retired instance.
- ✅ Profile switch очищає старі runtime bindings; unknown profile залишається effect-free.
- 🟡 Повний P/E/G/V presentation/update path ще не закритий.
- 🟡 V-local clock, A animation progression, cleanup/replay ще попереду.
- 🔴 Exact-head build, APK і physical Android acceptance ще не виконані для нового HEAD.

## Наступний bounded slice

Після цього pass — продовжити evidence-safe P/E/G/V graph update/presentation, починаючи з підтверджених child dispatch paths; невідомі P/G семантики залишаються preserved/undecoded до окремого доказу.
