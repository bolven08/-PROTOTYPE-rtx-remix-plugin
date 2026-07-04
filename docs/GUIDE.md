# PROTOTYPE RTX — Руководство по разработке

## Архитектура мода

```
Prototype.exe
│
├── dinput8.dll          ← НАШ МОД (загружается первым)
│   ├── d3d9_proxy.cpp   ← перехватывает CreateDevice
│   ├── prototype_hooks  ← перехватывает шейдерные константы
│   └── передаёт матрицы → SetTransform()
│
├── d3d9.dll             ← RTX Remix Bridge
└── .trex/
    └── d3d9.dll         ← RTX Remix Runtime (рейтрейсинг)
```

---

## Шаг 1: Сборка

### Требования
- Visual Studio 2022 (Community Edition)
- DirectX SDK June 2010
- MinHook (клонировать в `deps/minhook`)
- CMake 3.20+

### Команды
```bat
git clone https://github.com/TsudaKageyu/minhook deps/minhook
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A Win32
cmake --build . --config Release
```

Или открыть в VS2022 через `File → Open → CMake Project`.

---

## Шаг 2: Найти правильные регистры матриц

Это **самая важная часть**. Без правильных регистров Remix не поймёт,
где камера, и освещение будет неправильным.

### Инструменты
- **RenderDoc** (бесплатно): захватить кадр, посмотреть VS Constants
- **PIX for Windows**: аналогично
- **Cheat Engine**: найти матрицы в памяти по значению

### Как найти матрицы через RenderDoc

1. Запустить PROTOTYPE через RenderDoc (File → Launch Application)
2. Нажать F12 в игре для захвата кадра
3. В Event Browser найти первый `DrawIndexedPrimitive` с 3D геометрией
4. Открыть вкладку **VS Constants**
5. Искать 4 подряд идущих регистра (c0, c1, c2, c3), которые
   при повороте камеры изменяются — это View матрица

### Признаки View матрицы
Правый столбец содержит позицию камеры (большие числа = координаты в мире).
Верхние 3×3 — ортогональная матрица поворота (строки близки к единичным векторам).

### Как проверить найденные регистры
Запустить игру с `logMatrices = true` в конфиге.
Смотреть в `prototype_rtx.log` — строки View матрицы должны
меняться при повороте камеры.

### Обновить в коде
```cpp
// в prototype_rtx.h, struct Config:
int viewRegister  = 8;   // ← изменить на найденный
int projRegister  = 12;  // ← изменить на найденный
int worldRegister = 4;   // ← изменить на найденный
```

---

## Шаг 3: Определить какой DLL импортирует игра

Открыть `Prototype.exe` в **Explorer Suite (NTCore)** → PE Editor → Imports.
Найти DLL из этого списка, которую мы можем подменить:

| DLL | Вероятность |
|-----|-------------|
| `dinput8.dll` | Высокая (DirectInput) |
| `winmm.dll` | Высокая (мультимедиа) |
| `d3d9.dll` | Используем для Remix Bridge |

Переименовать наш мод в нужный DLL.

---

## Шаг 4: Структура папки игры

```
Prototype/
├── Prototype.exe
├── dinput8.dll          ← НАШ МОД (переименованный)
├── d3d9.dll             ← RTX Remix Bridge
├── prototype_rtx.log    ← создаётся автоматически
└── .trex/
    ├── d3d9.dll         ← RTX Remix Runtime
    └── bridge.conf
```

---

## Шаг 5: Известные проблемы PROTOTYPE

### Инстансинг персонажей
PROTOTYPE активно использует GPU instancing для толпы.
Remix не поддерживает instancing — придётся либо его отключить
(патч через хук `SetStreamSourceFreq`), либо смириться.

### Процедурная геометрия города
Чанки города стримятся динамически. Это нормально для Remix,
но может вызывать артефакты при первой загрузке.

### UI / HUD
Нужно определить шейдер HUD и либо его пропускать,
либо устанавливать Identity матрицу для 2D drawcall'ов.

---

## Шаг 6: Отладка

### Проверить что мод загрузился
```
Prototype/prototype_rtx.log должен появиться при запуске.
Строка "[PROTO-RTX] DLL attached to process." = успех.
```

### Проверить что Remix получает матрицы
В Remix меню (Alt+X) → Camera → должна показывать правильный FOV и позицию.

### Если тени "плывут"
Неверный регистр View или нужно попробовать `transposeMatrices = false`.

---

## Полезные ссылки

- [ViewProj Blog](https://zero-irp.github.io/ViewProj-Blog/) — теория матриц камеры в DX9
- [xoxor4d NFSC RTX](https://github.com/xoxor4d/nfsc-rtx) — пример аналогичного мода
- [RTX Remix Discord](https://discord.gg/rtxremix) — сообщество
- [RenderDoc](https://renderdoc.org/) — анализ шейдеров
