# Промпт для агента: DMC Native Reader — одне ядро на Android, macOS, iOS, Windows

Скопіюй усе нижче в нову сесію агента.

---

## 0. Підключи проект

1. Додай репозиторій **`VrUaCom/DMC-Native-Reader`** з правом push і клонуй його з підмодулями:
   `git clone --recurse-submodules https://github.com/VrUaCom/DMC-Native-Reader` → `git checkout NR-Luna-v73`.
2. Підмодуль `app/src/main/cpp/vendor/dmc-rengine-cpp` — це репозиторій `VrUaCom/dmc-rengine-cpp`. Лише читати.
3. Сховище реверсу Obsidian на Google Drive. Читати лише для специфікацій, див. правила clean room нижче:
   - тека `DMC Rengine`: https://drive.google.com/drive/folders/1hCkEgja4w6eV-XBchbXkNpp_g56ddjT6
   - сховище `DMC3-Reverse-Gemma`: https://drive.google.com/drive/folders/1hs9teqyn6XFqLRE9D2kLxynjJmCPwdAZ
4. Приватний `VrUaCom/dmc-rengine-recon` (C++ реверсу). Його читає лише той, хто пише специфікації, але не код ядра.

## 1. Де що лежить

Гілка `NR-Luna-v73`, база: https://github.com/VrUaCom/DMC-Native-Reader/tree/NR-Luna-v73

| Що | Де |
|---|---|
| План порту | [`docs/PLATFORM_PORT_PLAN.md`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/docs/PLATFORM_PORT_PLAN.md) |
| Межа ядра: таблиця 147 функцій JNI → C API, правила, баги | [`docs/port/C_API_MIGRATION.md`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/docs/port/C_API_MIGRATION.md) |
| Цей промпт | [`docs/port/AGENT_PROMPT.md`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/docs/port/AGENT_PROMPT.md) |
| Ядро C++23 | [`app/src/main/cpp`](https://github.com/VrUaCom/DMC-Native-Reader/tree/NR-Luna-v73/app/src/main/cpp) (`modules/`, `spider/`, `include/dmcresource/`) |
| C API (єдина межа для всіх оболонок) | [`capi/include/dmc_reader.h`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/app/src/main/cpp/capi/include/dmc_reader.h), [`capi/dmc_reader.cpp`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/app/src/main/cpp/capi/dmc_reader.cpp) |
| Спільний лок сесій | [`include/dmcresource/session_lock.h`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/app/src/main/cpp/include/dmcresource/session_lock.h) |
| Тести C API | [`app/src/test/native/c_api_test.c`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/app/src/test/native/c_api_test.c), [`c_api_threads_test.cpp`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/app/src/test/native/c_api_threads_test.cpp) |
| Android: JNI | [`app/src/main/cpp/app_native.cpp`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/app/src/main/cpp/app_native.cpp) (147 функцій) |
| Android: Java-оболонка | [`app/src/main/java/com/dmcrengine/nativeviewer`](https://github.com/VrUaCom/DMC-Native-Reader/tree/NR-Luna-v73/app/src/main/java/com/dmcrengine/nativeviewer) |
| Android: GPU-рендер (GLES3 + 67 шейдерів DMC3) | [`app/src/main/cpp/android/gles_view_backend.cpp`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/app/src/main/cpp/android/gles_view_backend.cpp) |
| macOS: SwiftUI-оболонка 0.1 | [`apple/`](https://github.com/VrUaCom/DMC-Native-Reader/tree/NR-Luna-v73/apple) (`build-mac.sh`, `Sources/DMCNativeReader/`) |
| Windows: Win32-оболонка | [`app/src/main/cpp/win_shell.cpp`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/app/src/main/cpp/win_shell.cpp), [`win_cli.cpp`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/app/src/main/cpp/win_cli.cpp) |
| Специфікації clean room | [`docs/cleanroom/`](https://github.com/VrUaCom/DMC-Native-Reader/tree/NR-Luna-v73/docs/cleanroom) |
| Перевірка JNI (сигнатури + лок) | [`tools/check_jni_signatures.py`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/tools/check_jni_signatures.py) |
| Перевірка APK на емуляторі | [`tools/emulator_smoke.sh`](https://github.com/VrUaCom/DMC-Native-Reader/blob/NR-Luna-v73/tools/emulator_smoke.sh) |
| CI | [`.github/workflows/`](https://github.com/VrUaCom/DMC-Native-Reader/tree/NR-Luna-v73/.github/workflows) |
| Релізи | https://github.com/VrUaCom/DMC-Native-Reader/releases (Android v151 / 1.0.124 — останній) |
| Нотатки релізів | [`docs/releases/`](https://github.com/VrUaCom/DMC-Native-Reader/tree/NR-Luna-v73/docs/releases) |

## 2. Правила (обов'язкові)

**Гілки й коміти:**
- Працювати лише на гілці `NR-Luna-v73`. У `main` не пушити ніколи. PR не створювати, якщо не попросять.
- Усе комітити й пушити (`git push origin NR-Luna-v73`).
- В артефактах репозиторію (коміти, код, доки) не згадувати ідентифікатори моделей.

**Файли гри:**
- Ніколи не комітити файли Capcom, виконувані файли чи дані гри. У репозиторії — лише ідентифікатори, зміщення й адреси.
- Тестові файли гри лежать тільки локально, поза репозиторієм.

**Clean room:**
- Хто бачив декомпільований код (сховище, recon), той пише лише специфікації в `docs/cleanroom/specs-*`.
- Код ядра за специфікацією пише окремий агент, який бачив лише специфікацію.
- Шейдерів Capcom не додавати ніколи.

**Ядро і оболонки:**
- Рішення приймає ядро (Spider / Black Widow). Оболонки лише передають дані й виконують кроки платформи.
- Перевикористовувати й розширювати наявний код.
- Вендорний `dmc-rengine-cpp` не редагувати.
- 8K не зменшувати: межа кадру — 64…8192 по кожній стороні.

**Reader:**
- Reader — лише переглядач DMC 1/2/3: нічого не редагує.
- Пріоритет — візуалізація. Будь-який MOD чи SCM має відкритися й показати, що з ним не так, а не впасти.

**Недовірені файли** (завантажені, з архівів):
- кожен у власну теку;
- Python, що їх читає, запускати з `-I`;
- їхніх скриптів ніколи не запускати.

**Роздача без платних підписів.** Apple нічого не платимо; хто хоче, той ставить:
- Android: APK;
- macOS: `.app` з підписом ad-hoc;
- iOS: непідписаний `.ipa`;
- Windows: `.exe` без підпису.

Відповіді користувачу — українською.

## 3. Де ми зараз (коміт `a66ebeb`)

**Ядро і C API:**
- Одне ядро C++23, близько 52 тис. рядків.
- C API: 44 функції. Група 1 вже є: відкриття, складання PAC/MOD, PTX, частини моделі, мініатюри, UV-галерея.
- JNI і C API беруть один лок сесій.

**Платформи:**
- Android готовий (v151): GPU, анімації, стейджі, ефекти, катсцени, фото камера, звіт падіння.
- macOS 0.1: відкриття, Info, здоров'я файлу, файли архіву, CPU-рендер. На Mac ще не збиралась.
- Windows: Win32 рівня v33, збирається лише MSVC.
- iOS: немає.

**Перевірки:**
- хостові тести GCC і clang + libc++: 55 з 55;
- Swift через C API на Linux відкриває й малює всі тестові файли;
- емулятор: 0 помилок.

## 4. Що робити — по черзі

**Фаза 1 — межа (C API ≈ JNI)**, за таблицею з `docs/port/C_API_MIGRATION.md`. Кожна група: функції в `dmc_reader.h`, тест у `c_api_test.c`, а якщо група змінює сесію, то й у `c_api_threads_test.cpp`. Після цього JNI цієї групи переписати як тонку обгортку над C API.
1. Група 2: рендер, вибір під пальцем (`dmc_pick`), утримання (`dmc_hold`), бенчмарк, експорт PNG/UV, камера, політ, жести, гіроскоп. Математика жестів переходить з `DmcRenderView.java` у ядро (#45). У `dmc_view` бракує статусу «показано у вікні».
2. B7: розбирати файл без локу; лок брати лише на зміну глобального стану стейджа.
3. B8: `black_widow_state` не має перемикати активний слот стейджа; слот передавати явно.
4. Групи 3–9: анімації, стейджі, меню, катсцени, відео, фото камера, діагностика. `motionScriptCount` прибрати.
5. Додати `dmc_api_version()`.

**Фаза 0 / 4 — macOS:**
- зібрати `apple/build-mac.sh` на M1 користувача (через Remote Control) і виправити, що не збереться;
- далі налаштування, анімації, стейджі, катсцени — до рівня Android.

**Фаза 2 — GPU поза Android.** Рішення користувача (2026-11-21): **свій Metal (macOS, iOS) і D3D11 (Windows)**, не ANGLE. Ядро вже дає бекенду незалежний від API кадр (`view_gpu.h`: `GpuViewFrame`, `GpuViewBackend`); кожен бекенд малює його так само, як `android/gles_view_backend.cpp`. Етапи:
1. ✅ (2026-11-21) Metal: власні шейдери бекенда (сітка, екранні квади) на MSL, усі проходи кадру, MSAA, mip-текстури. st101 960×720: 4,8 мс на M1 проти 93 мс на CPU. Пост-обробка фото-камери — теж на Metal (див. нижче). Шейдери збираються з тексту під час роботи (`newLibraryWithSource`), тож офлайн-компілятор `metal` не потрібен. Кадр малюється поза екраном і читається назад, як на Android.
2. Шейдери DMC3. ✅ Ті 6, якими ядро малює (`nr_draw`: model_lit, effect_model_lit, stage_lit, stage_lit_fog, texture_modulate, texture_modulate_fog), переписані на MSL у `apple/metal_view_backend.mm`. Решта 61 ще не підключені й на Android (світіння, DOF, відблиски); коли ядро почне ними малювати — переклад на MSL — SPIRV-Cross (glslang → SPIR-V → MSL) під час збирання або перекладач цього підмножини GLSL ES 3.00.
   ✅ Графіка → «Lighting (DMC3 shaders, GPU)»: «Enhanced» — те саме світло попіксельно (Metal), м'які відблиски й обвідка, моделі із запеченим у вершинні кольори світлом дістають світло камери (ядро, nr_draw; на Android — по вершинах). За замовчуванням як у грі.
   ✅ Прямий показ на macOS: кадр іде з GPU одразу на CAMetalLayer вікна (`dmc_view.window`, `DMC_RENDER_SHOWN`, `dmc_gpu_release`), без зчитування пікселів і CGImage; із фото-камерою — як раніше, через картинку. Застосунок тепер має одне вікно (файл із Finder відкривається в ньому).
   ✅ Обробка фото-камери (CameraPost) на Metal, як на Android: автофокус, глибина різкості, лінза/поляризатор/«look», різкість, оверлеї поверх. pl000 960×720 з DOF f/1.2: 9 мс проти 97 мс на CPU; кадр і далі йде прямо у вікно.
3. Показ напряму у вікно: `dmc_gpu_attach(native_window)` (CAMetalLayer), без читання назад.
4. D3D11 для Windows тим самим шляхом (HLSL).

**Фаза 3 — звук і відео:**
- miniaudio в ядрі (рішення ще не прийнято, пропозиція — так);
- мінімальний FFmpeg (ASF/WMV/VC-1/WMA) на всіх платформах.

**Фаза 5 — iOS:**
- спільний SwiftUI-код з macOS, «Файли», дотик;
- CI збирає непідписаний `.ipa`;
- опційно — веб-версія (Emscripten + WebGL 2).

**Фаза 6 — Windows:**
- оболонка через C API;
- прибрати конструкції, що збираються лише MSVC (`wstring` fstream, `wmain`, `<cmath>`);
- MinGW і MSVC у CI.

**Фаза 7 — релізи** всіх платформ з одного коміту.

## 5. Перевірка перед кожним пушем

- `python3 tools/check_jni_signatures.py app/src/main/java/com/dmcrengine/nativeviewer/NativeBridge.java app/src/main/cpp/app_native.cpp`
- Хостові тести: `cmake -S app/src/main/cpp -B build && cmake --build build -j && ctest --test-dir build`. Ще раз — clang з `-stdlib=libc++`.
- Перед будь-яким APK для людей: `gradle :app:assembleDebug -PdmcAbi=x86_64` (Gradle 9.5) і `tools/emulator_smoke.sh <apk> <тека з тестовими файлами>` на емуляторі.
- Перечитати свій diff: що змусить CI відхилити зміну?

## 6. Звітуй

Після кожного кроку — коротко українською:
- що зроблено;
- які баги знайдено;
- які перевірки пройшли;
- що далі.

Нові баги дописувати в розділ 2 `docs/port/C_API_MIGRATION.md`.
