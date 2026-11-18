# Межа ядра: перехід з JNI на C API

Цей документ — робочий план фази 1 з `docs/PLATFORM_PORT_PLAN.md`. Тут записано:
- правила, за якими C API (`app/src/main/cpp/capi/include/dmc_reader.h`) стає єдиною межею ядра для Android, Windows, macOS та iOS;
- баги, знайдені під час огляду всіх 147 функцій JNI;
- куди переходить кожна функція JNI.

Таблицю складено з аудиту `app_native.cpp` (147 функцій, 2,5 тис. рядків) і з місць, де Java їх кличе. Перевірка `tools/check_jni_signatures.py` тримає Java і C++ у згоді.

## 1. Правила межі

**Рішення приймає ядро.** Оболонка:
- читає файли;
- показує діалоги, вікна і меню, які описало ядро;
- зберігає вибір людини;
- передає ядру дотики й датчики.

Порядок відкриття, назви, тексти Info, стан кнопок вирішує Spider / Black Widow. Якщо функцію C API хочеться назвати `should_*` або `pick_*` на боці оболонки, це ознака, що рішення ще не в ядрі.

**Рядки.** `char*`, який повертає C API, належить тому, хто викликав, і звільняється через `dmc_string_free`. Усі рядки — UTF-8; ядро ріже їх за символами (`text_utf8.h`), а не за байтами.

**Списки** (`dmc_settings`, `dmc_motion_strip`, `dmc_parts`, `dmc_strings`) — це знімок у момент виклику. Вказівники всередині живуть, поки список не звільнено. Після зміни сесії список будують знову.

**Буфери пікселів** виділяє оболонка і передає їх місткість. Якщо буфер замалий, функція нічого не пише й повертає 0/false. Через це `allocateFrameBuffer` / `freeFrameBuffer` у C API не переходять.

**Сесії й володіння:**
- кожна сесія від `dmc_open`, `dmc_open_source`, `dmc_open_child`, `dmc_assemble`, `dmc_compose` або `dmc_open_uv_gallery` належить тому, хто викликав, і закривається один раз;
- сесія, зібрана з інших, тримає власні копії (`shared_ptr` на байти), тому джерела можна закрити раніше (перевірено в `c_api_test`);
- JNI тримає реєстр живих сесій (`live_sessions`), тож закритий handle там нічого не робить. У C API реєстру немає, і подвійне закриття — помилка оболонки. Swift закриває сесію в `deinit`, тож такої помилки там не буває. Для Windows слід зробити так само: RAII-обгортка над `dmc_session*`.

**Потоки.**
- Ядро має один лок сесій: `dmcresource::session_mutex()` (`session_lock.h`). Його беруть і JNI, і C API.
- Кадр малюється під цим локом. Кожен виклик, що читає або змінює сесію, тримає лок увесь час, поки працює з нею.
- Стан ядра поза сесіями має власні локи, і лок сесій йому не потрібен:
  - кімната стейджа (`stage_room`, м'ютекс і знімки `shared_ptr`);
  - камера (`camera_control`, м'ютекс);
  - налаштування фото камери та гіроскоп (атомарні змінні, м'ютекс).
- Правило для коду ядра: стан кімнати читати **одним знімком** (`const auto room = stage_room::current();`), а не двома викликами підряд.

**Помилки.** Жодна функція C API не кидає винятків назовні. Помилка повертається як NULL, false або 0, а причину пише ядро: у тексті Info, у назві статусу, у заголовку відхиленого файлу. Відповідно до принципу Reader'а пріоритет має візуалізація: файл, який не вдалося розібрати, відкривається як опис, а не падає.

**Версія.** Поки межа росте, кожна нова функція додається в кінець групи, а старі не змінюються. Перед першим релізом не-Android платформи треба додати `dmc_api_version()`. Тоді Swift-пакет зможе перевіряти, що його зібрано з тим самим ядром.

## 2. Баги, знайдені під час огляду

| # | Де | Що | Стан |
|---|---|---|---|
| B1 | `app_native.cpp`: `attachModPartToHostJoint`, `resetModPartPlacement`, `compositePartState`, `compositePartNodeCount`, `compositePartNodeName`, `compositePartDefaultAttachmentSelector`, `diagnostics` | Без локу сесії. `from_handle` брав лок лише на перевірку handle і відпускав його. Прикріплення частини MOD переписує `render_mesh.vertices`, `scene.nodes` і `hierarchy_overlay`, поки потік рендера малює ту саму сесію: це рвані вершини або падіння під час перерозподілу пам'яті. | виправлено: лок на весь виклик; `check_jni_signatures.py` тепер падає на будь-якій функції, що кличе `from_handle` до локу |
| B2 | JNI і C API | Два різні м'ютекси (`session_mutex` у JNI і `api_mutex` у C API), які одне одного не виключали. | виправлено: один лок у ядрі, `session_lock.h` |
| B3 | `resource_session.cpp`, `black_widow_state` | `current() != nullptr && current()->broken`: кімнату читали двічі. Якщо між двома читаннями інший потік її прибере (`clearRoom`), буде звертання до null. Цей стан читають і потік рендера, і UI. | виправлено: один знімок |
| B4 | `writeSource` | Умова «`ftruncate(...) == 0` або `written == size`» завжди істинна. Якщо файл, поверх якого зберігають, був довшим і обрізати його не вдалося, старий хвіст лишається, а відповідь — «успіх». | виправлено: звичайний файл, який не вдалося обрізати, дає помилку; потік без довжини (pipe, provider) — ні |
| B5 | macOS `ViewerModel.show` | Скидання камери просило кадр ще до того, як модель дізнавалася, що сесія — картинка: зайвий рендер у черзі. | виправлено |
| B6 | `motionScriptCount` | Мертва функція: Java її не кличе. | прибрано з JNI і `NativeBridge.java` (146 функцій) |
| B7 | `openResource`, `dmc_open` | Лок тримається, поки йде все відкриття. Великий PAC зупиняє кадри на час розбору. | план: розбирати файл без локу (розбір нічого спільного не змінює), а лок брати лише на зміну глобального стану стейджа |
| B8 | `black_widow_state` | Запит стану має побічну дію: `set_active_slot(slot_for(session))`. Якщо кадр однієї сесії і меню іншої читають стан одночасно, активний слот стейджа може переключитися. | план: слот передавати явно в групі 4 |
| B9 | C API | Немає реєстру handle: подвійне `dmc_close` — це UB. | правило з розділу 1; у debug-збірці можна додати реєстр, як у JNI |
| B10 | `crash_recorder.cpp` на macOS / iOS | Обробник вирішує, чи сигнал надіслано через `raise()`/`kill()`, за `si_code <= 0`. Так позначає лише Linux; Darwin дає додатні коди (`raise(SIGILL)` приходить з кодом 2), тож обробник не перевикликав сигнал і процес жив далі після «падіння». Тест `crash_recorder` на Mac падав. | виправлено: на Apple сигнал завжди перевикликається з відновленою стандартною дією; Linux/Android — без змін (збій повертається до інструкції, щоб системний звіт бачив справжній контекст) |
| B11 | збірка clang на arm64 (Mac, iOS, пристрої Android) | clang за замовчуванням зливає `a*b+c` в одну операцію з одним округленням (FMA). Колір частинки ставав 59 замість 60, як в еталонах x86; тест `particle` на Mac падав, а пристрої arm64 малювали інакше, ніж хостові еталони. | виправлено: `-ffp-contract=off` для всіх компіляторів, крім MSVC; на Mac 55 з 55 |

## 3. Куди переходить кожна функція

Пояснення до стовпців:
- «Java» — де Java кличе функцію і скільки разів;
- «лок сесії» показує стан після виправлень: «—» означає, що функція не працює з сесією (стан кімнати, камери, налаштувань має власні локи);
- група — це група з розділу 3.2 плану порту.

Підсумок:
- 17 функцій уже є в C API v1;
- група 1 (24 функції) додана в цьому кроці; C API вже має кожну функцію з її таблиці;
- решта — групи 2–10.

### 0. Уже в C API (v1) — 17

| JNI | Java | лок сесії | C API | примітка |
|---|---|---|---|---|
| `applySettings` | MainActivity | — | `dmc_apply_settings` | є |
| `blackWidowState` | DmcRenderView, MainActivity | так | `dmc_state_flags` | є |
| `childResourceCount` | MainActivity, ChildResourceBrowserView | так | `dmc_child_count / dmc_child_title` | є |
| `childResourceTitle` | MainActivity, ChildResourceBrowserView | так | `dmc_child_count / dmc_child_title` | є |
| `close` | MainActivity | так | `dmc_close` | є |
| `fileHealth` | MainActivity | так | `dmc_file_health` | є |
| `imagePreview` | DmcRenderView | так | `dmc_image_preview_size / dmc_image_preview` | є; три виклики JNI стають двома |
| `imagePreviewHeight` | DmcRenderView | так | `dmc_image_preview_size / dmc_image_preview` | є; три виклики JNI стають двома |
| `imagePreviewWidth` | DmcRenderView | так | `dmc_image_preview_size / dmc_image_preview` | є; три виклики JNI стають двома |
| `motionStrip` | MainActivity | так | `dmc_motion_strip_build` | є |
| `openChild` | MainActivity | так | `dmc_open_child` | є |
| `openResource` | MainActivity | так | `dmc_open` | є з v1 |
| `renderToBuffer` | DmcRenderView | так | `dmc_render` | є; бракує полів dmc_view: room_yaw, follow, dolly і статусу «показано у вікні» (біт 4) |
| `sessionInfoText` | MainActivity | так | `dmc_info_text` | є |
| `settingsSchema` | MainActivity | — | `dmc_settings_schema / dmc_settings_selected` | є |
| `settingsSelection` | MainActivity | — | `dmc_settings_schema / dmc_settings_selected` | є |
| `welcomeText` | MainActivity | — | `dmc_welcome_text` | є |

### 1. Відкриття і складання — 24 (додано в C API)

| JNI | Java | лок сесії | C API | примітка |
|---|---|---|---|---|
| `archiveVariantNames` | MainActivity | так | `dmc_variant_names → dmc_strings` |  |
| `assemblePacs` | MainActivity | так | `dmc_assemble(names[], bytes[], sizes[], n)` | вибір порядку і назви вже в ядрі (assemble_archives) |
| `attachModPartToHostJoint` | MainActivity | так (v152) | `dmc_part_attach / dmc_part_reset` | змінюють сітку, яку малює потік рендера: тільки під блокуванням (виправлено у v152) |
| `attachPtx` | MainActivity | так | `dmc_attach_ptx(session, bytes, size, part or -1)` | одна функція: part = -1 означає вся модель |
| `attachPtxToPart` | MainActivity | так | `dmc_attach_ptx(session, bytes, size, part or -1)` | одна функція: part = -1 означає вся модель |
| `childResourcePreview` | ChildResourceBrowserView | так | `dmc_child_preview_size / dmc_child_preview` | як image_preview |
| `childResourcePreviewHeight` | ChildResourceBrowserView | так | `dmc_child_preview_size / dmc_child_preview` | як image_preview |
| `childResourcePreviewWidth` | ChildResourceBrowserView | так | `dmc_child_preview_size / dmc_child_preview` | як image_preview |
| `composeMods` | MainActivity | так | `dmc_compose(sessions[], n)` | сесії-джерела лишаються у власника |
| `compositePartCount` | MainActivity | так | `dmc_parts(session) → dmc_part_list` | шість викликів на частину стають одним списком структур; до v152 частина з них була без блокування |
| `compositePartDefaultAttachmentSelector` | MainActivity | так (v152) | `dmc_parts(session) → dmc_part_list` | шість викликів на частину стають одним списком структур; до v152 частина з них була без блокування |
| `compositePartName` | MainActivity | так | `dmc_parts(session) → dmc_part_list` | шість викликів на частину стають одним списком структур; до v152 частина з них була без блокування |
| `compositePartNodeCount` | MainActivity | так (v152) | `dmc_parts(session) → dmc_part_list` | шість викликів на частину стають одним списком структур; до v152 частина з них була без блокування |
| `compositePartNodeName` | MainActivity | так (v152) | `dmc_parts(session) → dmc_part_list` | шість викликів на частину стають одним списком структур; до v152 частина з них була без блокування |
| `compositePartState` | MainActivity | так (v152) | `dmc_parts(session) → dmc_part_list` | шість викликів на частину стають одним списком структур; до v152 частина з них була без блокування |
| `hasDualPreview` | MainActivity | так | `dmc_state_flags + dmc_set_info_preview` | прапорці стану вже є; лишається один setter |
| `infoPreviewActive` | MainActivity | так | `dmc_state_flags + dmc_set_info_preview` | прапорці стану вже є; лишається один setter |
| `open` | MainActivity | — | `dmc_open_source(name, bytes, size)` | джерело для складання (PAC, MOD, PTX) без рішень open_resource; Java кличе її в чотирьох місцях складання |
| `openUvGallery` | MainActivity | так | `dmc_open_uv_gallery` |  |
| `resetModPartPlacement` | MainActivity | так (v152) | `dmc_part_attach / dmc_part_reset` | змінюють сітку, яку малює потік рендера: тільки під блокуванням (виправлено у v152) |
| `setInfoPreviewActive` | MainActivity | так | `dmc_state_flags + dmc_set_info_preview` | прапорці стану вже є; лишається один setter |
| `sourceFileName` | MainActivity | так | `dmc_source(session) → name + bytes` | запис файлу робить оболонка (діалог збереження) |
| `textureAttachmentInfo` | MainActivity | так | `dmc_info_text (розділ текстур)` | текст ядра; окремої функції не треба |
| `writeSource` | MainActivity | так | `dmc_source(session) → name + bytes` | запис файлу робить оболонка (діалог збереження) |

### 2. Рендер, вибір, камера перегляду — 25 (додано в C API)

Усі функції групи є в C API:
- вибір і утримання: `dmc_pick`; `dmc_hold` повертає `dmc_outcome` з кроком оболонки `dmc_shell_step`;
- камера: `dmc_camera_metrics`, `dmc_camera_mode`;
- жести: `dmc_claim_gesture`, `dmc_end_touch`, `dmc_camera_reset`, `dmc_zoom_step`, `dmc_camera_level`, `dmc_camera_roll`, `dmc_camera_holds_orientation`;
- гіроскоп: `dmc_gyro_*`;
- політ: `dmc_fly_enter`, `dmc_fly_tick`, `dmc_fly_leave`, `dmc_stick_knob`, `dmc_walk`, `dmc_walk_active`;
- видошукач: `dmc_viewfinder` → `dmc_finder`;
- експорт і фото камера: `dmc_export_image_size`, `dmc_render_export`, `dmc_render_capture` → `dmc_image` (розмір знімка вирішує ядро);
- бенчмарк: `dmc_benchmark`.

`dmc_render` повертає біт `DMC_RENDER_SHOWN` (4), коли кадр пішов просто у вікно.

Перевірено двома тестами, обидва проходять і під ThreadSanitizer на macOS:
- `c_api_test.c`: вибір моделі на сітці (модель знаходиться лише там, де вона намальована), утримання, метрики, жести, політ, видошукач, експорт (у моделі його немає), знімок, бенчмарк;
- `c_api_threads_test.cpp`: вибір, утримання, метрики і знімок між кадрами іншого потоку.

#45 — математика жестів у ядрі: `view_gesture` (`dmc_view_reset`, `dmc_view_drag`, `dmc_view_pan`, `dmc_view_twist`, `dmc_angle_delta`, `dmc_pinch_zoom`, `dmc_fling`, `dmc_spin_step`). Числа ті самі, що були в `DmcRenderView.java`: 0,008 рад/px, межа нахилу 1,55, зум 0,15…8, панорама 2,96 / (зум × коротша сторона), поріг кидка 900 × щільність / 2,5 px/с, інерція −92 % за секунду. macOS-оболонка вже рухає камеру лише через них. Android ще рахує сам: `DmcRenderView` перейде на ці функції разом із переходом JNI на C API.

| JNI | Java | лок сесії | C API | примітка |
|---|---|---|---|---|
| `benchmarkView` | DmcRenderView | так | `dmc_benchmark(session, view, frames) → text` |  |
| `cameraHoldsOrientation` | MainActivity | — | `dmc_gyro_*` | датчики читає оболонка; ядро лише рахує поворот |
| `cameraLevel` | MainActivity | — | `dmc_gesture_* / dmc_camera_reset` | математика жестів #45: з DmcRenderView у ядро |
| `cameraMetrics` | DmcRenderView | так | `dmc_camera_metrics(session) → {distance, dolly_limit}` |  |
| `cameraMode` | DmcRenderView, MainActivity | — | `dmc_camera_mode(bool) → bool` |  |
| `cameraRoll` | DmcRenderView | — | `dmc_gesture_* / dmc_camera_reset` | математика жестів #45: з DmcRenderView у ядро |
| `claimGesture` | DmcRenderView | — | `dmc_gesture_* / dmc_camera_reset` | математика жестів #45: з DmcRenderView у ядро |
| `endTouch` | DmcRenderView | — | `dmc_gesture_* / dmc_camera_reset` | математика жестів #45: з DmcRenderView у ядро |
| `enterFly` | DmcRenderView | так | `dmc_fly_*` | стан камери вже в ядрі (camera_control, атомарні) |
| `exportImageSize` | MainActivity | так | `dmc_export_image_size / dmc_render_export` | PNG кодує оболонка або ядро (stb_image_write) — вирішити в групі |
| `flyTick` | DmcRenderView | так | `dmc_fly_*` | стан камери вже в ядрі (camera_control, атомарні) |
| `gyroActive` | MainActivity | — | `dmc_gyro_*` | датчики читає оболонка; ядро лише рахує поворот |
| `gyroReset` | MainActivity | — | `dmc_gyro_*` | датчики читає оболонка; ядро лише рахує поворот |
| `gyroTurn` | MainActivity | — | `dmc_gyro_*` | датчики читає оболонка; ядро лише рахує поворот |
| `holdView` | DmcRenderView | так | `dmc_hold(session, view, x, y) → dmc_card` | картка: заголовок, рядки, дії |
| `leaveFly` | DmcRenderView | так | `dmc_fly_*` | стан камери вже в ядрі (camera_control, атомарні) |
| `pickView` | DmcRenderView | так | `dmc_pick(session, view, x, y) → dmc_pick_result` |  |
| `renderCapture` | DmcRenderView | так | `dmc_render_capture` | кадр фото камери в повному розмірі |
| `renderExportImage` | MainActivity | так | `dmc_export_image_size / dmc_render_export` | PNG кодує оболонка або ядро (stb_image_write) — вирішити в групі |
| `resetCamera` | DmcRenderView | — | `dmc_gesture_* / dmc_camera_reset` | математика жестів #45: з DmcRenderView у ядро |
| `stickKnob` | DmcRenderView | — | `dmc_fly_*` | стан камери вже в ядрі (camera_control, атомарні) |
| `viewfinder` | DmcRenderView | так | `dmc_viewfinder(view) → dmc_strings` |  |
| `walk` | MainActivity | — | `dmc_fly_*` | стан камери вже в ядрі (camera_control, атомарні) |
| `walkActive` | MainActivity | — | `dmc_fly_*` | стан камери вже в ядрі (camera_control, атомарні) |
| `zoomStep` | DmcRenderView | — | `dmc_gesture_* / dmc_camera_reset` | математика жестів #45: з DmcRenderView у ядро |

### 3. Анімації — 11 (додано в C API)

У C API: `dmc_load_motion`, `dmc_load_library_motion`, `dmc_load_scripted_motion` (звіт ядра одним рядком), `dmc_motion_script_can_play`, `dmc_motion_script_slot`, `dmc_motion_range` (кінець і початок петлі, false без руху), `dmc_class_events` → `dmc_strings`, `dmc_trigger_class_event`. `hasMotion` — біт `dmc_state_flags` і `dmc_motion_range`. Перевірено в `c_api_test.c` і `c_api_threads_test.cpp` (завантаження руху між кадрами іншого потоку).

| JNI | Java | лок сесії | C API | примітка |
|---|---|---|---|---|
| `classEventNames` | MainActivity | так | `dmc_class_events / dmc_trigger_class_event` |  |
| `hasMotion` | DmcRenderView, MainActivity | так | `dmc_state_flags` | прапорець |
| `loadLibraryMotion` | MainActivity | так | `dmc_load_motion(session, bytes, size, kind, slot)` |  |
| `loadLibraryMotionScript` | MainActivity | так | `dmc_load_motion(session, bytes, size, kind, slot)` |  |
| `loadMotion` | MainActivity | так | `dmc_load_motion(session, bytes, size, kind, slot)` |  |
| `motionEndFrame` | DmcRenderView | так | `dmc_motion_range(session) → {end, loop_start}` |  |
| `motionLoopStartFrame` | DmcRenderView | так | `dmc_motion_range(session) → {end, loop_start}` |  |
| `motionScriptCanPlayMotion` | MainActivity | так | `dmc_motion_strip (поля картки)` | стрічка вже несе ці відповіді |
| `motionScriptCount` | — | так | `— прибрати` | Java її не викликає |
| `motionScriptSlot` | MainActivity | так | `dmc_motion_strip (поля картки)` | стрічка вже несе ці відповіді |
| `triggerClassEvent` | MainActivity | так | `dmc_class_events / dmc_trigger_class_event` |  |

### 4. Стейджі — 27 (додано в C API)

У C API:
- кімната (стан ядра, власний лок, без сесії): `dmc_room_load`, `dmc_room_clear`, `dmc_room_set_broken`, `dmc_room_spot_count`, `dmc_room_collision_source_count`, `dmc_room_animated`, `dmc_base_spawn_points`, `dmc_place_at_spawn`, `dmc_next_base_spot`, `dmc_after_new_room`, `dmc_preset_label`, `dmc_base_room_menu` → `dmc_menu` (з позначками ready / warn);
- ефекти й конфігурація: `dmc_room_effects_load` / `_clear`, `dmc_stage_config_load` / `_clear`, `dmc_stage_archives`, `dmc_stage_slot`, `dmc_stage_start_view`, `dmc_stage_stem`;
- колізії: `dmc_stage_collision_source_count`, `dmc_collision_kinds`, `dmc_collision_attack_ids`, `dmc_select_collision_attack`, `dmc_collision_report` → `dmc_report`;
- картки маркерів: `dmc_marker_card` → `dmc_card`, `dmc_run_card_action` (відкритий файл — нова сесія того, хто кличе).

Спільні типи `dmc_menu`, `dmc_card`, `dmc_report` підуть і в групу 5. B8 у цій групі: слот передається явно (`slot` 0 / 1) у завантаження й очищення; `dmc_stage_config_load` ще ставить активний слот, як і JNI, бо так його читає `load_stage_config`.

| JNI | Java | лок сесії | C API | примітка |
|---|---|---|---|---|
| `baseRoomMenu` | MainActivity | так | `dmc_base_room_menu → dmc_menu` |  |
| `baseSpawnPoints` | MainActivity | — | `dmc_room_* (кімната — стан ядра, а не сесії)` | stage_room має свій м’ютекс; C API має це записати в контракт |
| `clearRoom` | MainActivity | — | `dmc_room_* (кімната — стан ядра, а не сесії)` | stage_room має свій м’ютекс; C API має це записати в контракт |
| `clearRoomEffects` | MainActivity | — | `dmc_room_effects_load / _clear` |  |
| `clearStageConfig` | MainActivity | — | `dmc_stage_*` |  |
| `collisionAttackIds` | MainActivity | так | `dmc_collision_*` |  |
| `collisionKinds` | MainActivity | так | `dmc_collision_*` |  |
| `collisionReport` | MainActivity | так | `dmc_collision_*` |  |
| `loadRoom` | MainActivity | — | `dmc_room_* (кімната — стан ядра, а не сесії)` | stage_room має свій м’ютекс; C API має це записати в контракт |
| `loadRoomEffects` | MainActivity | — | `dmc_room_effects_load / _clear` |  |
| `loadStageConfig` | MainActivity | так | `dmc_stage_*` |  |
| `markerCard` | MainActivity | так | `dmc_marker_card / dmc_run_card_action` |  |
| `nextBaseSpot` | MainActivity | — | `dmc_room_* (кімната — стан ядра, а не сесії)` | stage_room має свій м’ютекс; C API має це записати в контракт |
| `placeAtSpawn` | MainActivity | — | `dmc_room_* (кімната — стан ядра, а не сесії)` | stage_room має свій м’ютекс; C API має це записати в контракт |
| `presetLabel` | MainActivity | — | `dmc_base_room_menu → dmc_menu` |  |
| `roomAnimated` | DmcRenderView | так | `dmc_room_* (кімната — стан ядра, а не сесії)` | stage_room має свій м’ютекс; C API має це записати в контракт |
| `roomChange` | MainActivity | — | `dmc_room_* (кімната — стан ядра, а не сесії)` | stage_room має свій м’ютекс; C API має це записати в контракт |
| `roomCollisionSourceCount` | MainActivity | — | `dmc_collision_*` |  |
| `roomSpotCount` | MainActivity | — | `dmc_room_* (кімната — стан ядра, а не сесії)` | stage_room має свій м’ютекс; C API має це записати в контракт |
| `runMarkerCardAction` | MainActivity | так | `dmc_marker_card / dmc_run_card_action` |  |
| `selectCollisionAttack` | MainActivity | так | `dmc_collision_*` |  |
| `setRoomBroken` | MainActivity | — | `dmc_room_* (кімната — стан ядра, а не сесії)` | stage_room має свій м’ютекс; C API має це записати в контракт |
| `stageArchives` | MainActivity | — | `dmc_stage_*` |  |
| `stageCollisionSourceCount` | MainActivity | так | `dmc_collision_*` |  |
| `stageSlot` | MainActivity | так | `dmc_stage_*` |  |
| `stageStartView` | DmcRenderView | так | `dmc_stage_*` |  |
| `stageStem` | MainActivity | так | `dmc_stage_*` |  |

### 5. Меню і дії — 6

| JNI | Java | лок сесії | C API | примітка |
|---|---|---|---|---|
| `companionMenu` | MainActivity | так | `dmc_menu(kind) / dmc_run_action(id)` | одна форма меню на всі: рядки, ✕, дії |
| `effectMenu` | MainActivity | так | `dmc_menu(kind) / dmc_run_action(id)` | одна форма меню на всі: рядки, ✕, дії |
| `playEffect` | MainActivity | так | `dmc_menu(kind) / dmc_run_action(id)` | одна форма меню на всі: рядки, ✕, дії |
| `runCompanionAction` | MainActivity | так | `dmc_menu(kind) / dmc_run_action(id)` | одна форма меню на всі: рядки, ✕, дії |
| `soundMenu` | MainActivity | так | `dmc_menu(kind) / dmc_run_action(id)` | одна форма меню на всі: рядки, ✕, дії |
| `soundPcm` | MainActivity | так | `dmc_sound_pcm → dmc_pcm` | або відтворення в ядрі (miniaudio) |

### 6. Катсцени — 9

| JNI | Java | лок сесії | C API | примітка |
|---|---|---|---|---|
| `cameraFollowsCuts` | MainActivity | так | `dmc_cutscene_*` | вібрацію виконує оболонка |
| `demoAudioFormat` | CutsceneAudio | так | `dmc_cutscene_audio_*` | з miniaudio зникають з межі |
| `demoAudioPosition` | CutsceneAudio | так | `dmc_cutscene_audio_*` | з miniaudio зникають з межі |
| `demoAudioRead` | CutsceneAudio | так | `dmc_cutscene_audio_*` | з miniaudio зникають з межі |
| `demoSubtitle` | DmcRenderView | так | `dmc_cutscene_*` | вібрацію виконує оболонка |
| `demoSubtitleStyle` | DmcRenderView | так | `dmc_cutscene_*` | вібрацію виконує оболонка |
| `demoTakeCamera` | DmcRenderView | так | `dmc_cutscene_*` | вібрацію виконує оболонка |
| `demoVibration` | DmcRenderView | так | `dmc_cutscene_*` | вібрацію виконує оболонка |
| `loadCutsceneSound` | MainActivity | так | `dmc_cutscene_audio_*` | з miniaudio зникають з межі |

### 7. Відео — 2

| JNI | Java | лок сесії | C API | примітка |
|---|---|---|---|---|
| `beginVideo` | MainActivity | — | `dmc_video_begin / dmc_video_frame` | FFmpeg на всіх платформах |
| `renderVideoFrame` | DmcRenderView | так | `dmc_video_begin / dmc_video_frame` | FFmpeg на всіх платформах |

### 8. Фото камера — 12

| JNI | Java | лок сесії | C API | примітка |
|---|---|---|---|---|
| `cameraDials` | MainActivity | — | `dmc_camera_settings → dmc_settings` | та сама типізована схема, що й налаштування перегляду, замість рядків з табами |
| `cameraFocusAt` | DmcRenderView | так | `dmc_camera_focus_* / dmc_camera_polarizer_*` |  |
| `cameraFocusLock` | DmcRenderView | — | `dmc_camera_focus_* / dmc_camera_polarizer_*` |  |
| `cameraFocusRing` | DmcRenderView | — | `dmc_camera_focus_* / dmc_camera_polarizer_*` |  |
| `cameraFollowUps` | MainActivity | — | `dmc_camera_settings → dmc_settings` | та сама типізована схема, що й налаштування перегляду, замість рядків з табами |
| `cameraPlan` | DmcRenderView, ViewRecorder, MainActivity | — | `dmc_camera_settings → dmc_settings` | та сама типізована схема, що й налаштування перегляду, замість рядків з табами |
| `cameraPolarizer` | DmcRenderView, MainActivity | — | `dmc_camera_focus_* / dmc_camera_polarizer_*` |  |
| `cameraPolarizerTurnable` | DmcRenderView | — | `dmc_camera_focus_* / dmc_camera_polarizer_*` |  |
| `cameraSettings` | MainActivity | — | `dmc_camera_settings → dmc_settings` | та сама типізована схема, що й налаштування перегляду, замість рядків з табами |
| `setCameraChoices` | MainActivity | — | `dmc_camera_settings → dmc_settings` | та сама типізована схема, що й налаштування перегляду, замість рядків з табами |
| `setCameraPolarizer` | MainActivity | — | `dmc_camera_focus_* / dmc_camera_polarizer_*` |  |
| `writeCaptureDng` | DmcRenderView | так | `dmc_capture_dng → bytes` | запис файлу — оболонка |

### 9. Діагностика — 11

| JNI | Java | лок сесії | C API | примітка |
|---|---|---|---|---|
| `benchmarkSettings` | MainActivity | — | `dmc_settings_benchmark` |  |
| `buildIdentity` | MainActivity | — | `dmc_build_identity` |  |
| `diagnostics` | MainActivity | так (v152) | `dmc_diagnostics / dmc_inspect` |  |
| `inspectionTopic` | MainActivity | так | `dmc_diagnostics / dmc_inspect` |  |
| `installCrashRecorder` | CrashReports | — | `dmc_crash_install(path) / dmc_crash_context` | на Apple вже збирається (гілка ucontext) |
| `reencodeTextures` | MainActivity | так | `dmc_reencode_textures` |  |
| `reencodeTexturesDetail` | MainActivity | так | `dmc_reencode_textures` |  |
| `setCrashContext` | CrashReports | — | `dmc_crash_install(path) / dmc_crash_context` | на Apple вже збирається (гілка ucontext) |
| `testCrash` | MainActivity | — | `dmc_test_crash (лише debug)` |  |
| `textureFormatLabels` | MainActivity | — | `dmc_reencode_textures` |  |
| `textureFormatNames` | MainActivity | — | `dmc_reencode_textures` |  |

### 10. Лише платформа — 3

| JNI | Java | лок сесії | C API | примітка |
|---|---|---|---|---|
| `allocateFrameBuffer` | DmcRenderView | — | `— (буфер виділяє оболонка)` | C API пише в буфер того, хто кличе |
| `freeFrameBuffer` | DmcRenderView | — | `— (буфер виділяє оболонка)` | C API пише в буфер того, хто кличе |
| `setPresentSurface` | MainActivity | — | `dmc_gpu_attach(native_window, kind)` | єдине місце, де вікно платформи заходить у ядро |

## 4. Як Android переходить на C API

Java не змінюється. Змінюється лише тіло кожної функції JNI: замість прямого виклику ядра вона кличе відповідну `dmc_*`.

Порядок дій:
1. Додати групу в C API разом із перевіркою в `c_api_test.c`. Якщо група змінює сесію, ще й `c_api_threads_test.cpp`.
2. Переписати функції JNI цієї групи через C API: перетворення `jstring` ↔ `char*`, масивів і буферів, і нічого більше. `dmc_session*` і `jlong` переводяться одне в одне через той самий реєстр `live_sessions`.
3. `tools/check_jni_signatures.py` (сигнатури й лок), хостові тести GCC + libc++, `tools/emulator_smoke.sh` на емуляторі. Реліз Android — лише після цього.

Коли всі групи перейдуть, у `app_native.cpp` лишиться тільки перетворення типів. Тоді Android перевірятиме ту саму межу, що й macOS, iOS та Windows.

**Готово в цьому кроці:**
- група 1: відкриття джерел, складання PAC, композиція MOD, PTX, частини моделі, мініатюри дітей, UV-галерея, Info-картинка, вихідний файл;
- спільний лок;
- тест потоків.

**Група 2** теж додана в C API (див. розділ 3).

JNI обох груп досі кличе ядро напряму. Щоб він став обгорткою над C API, `jlong` має стати `dmc_session*`: реєстр `live_sessions` триматиме `dmc_session*`. Це одна зміна для всіх груп разом, і її треба перевірити Android-збіркою та емулятором.

**Наступний крок:** #45 (математика жестів у ядро), далі групи 3–9.

## 5. Що оболонка робить сама на кожній платформі

| Що | Android | macOS | iOS | Windows |
|---|---|---|---|---|
| Відкрити файл | `ContentResolver` + fd | `NSOpenPanel`, drop, Finder | document picker («Файли») | `GetOpenFileNameW`, drop |
| Зберегти файл (`dmc_source`, PNG, DNG) | SAF | `NSSavePanel` | share sheet | `GetSaveFileNameW` |
| Вікно для GPU (`dmc_gpu_attach`) | `ANativeWindow` | `CAMetalLayer` (ANGLE) | `CAMetalLayer` (ANGLE) | `HWND` (ANGLE / D3D11) |
| Дотики й жести | `MotionEvent` | миша, трекпад | `UIGestureRecognizer` | миша |
| Гіроскоп | `SensorManager` | — | Core Motion | — |
| Вібрація катсцени | `Vibrator` | — | Core Haptics | — |
| Звук (до miniaudio) | `AudioTrack` | AVAudioEngine | AVAudioEngine | WASAPI |
| Звіт падіння | `CrashReports` (Java) + `crash_recorder` | `crash_recorder` + показ при запуску | те саме | SEH-фільтр (треба дописати) |
| Роздача | APK, тестовий ключ | `.app`, ad-hoc | непідписаний `.ipa`, ставить сама людина | `.exe` без підпису |
