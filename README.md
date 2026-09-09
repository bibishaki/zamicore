# ZAMICORE

> **Deterministic Cognitive Operating System & Epistemic Substrate on FreeBSD / OpenZFS**

---
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.22313830.svg)](https://doi.org/10.5281/zenodo.22313830)
[![License: BSD 2-Clause](https://img.shields.io/badge/License-BSD_2--Clause-blue.svg)](LICENSE)
[![Specification: v2.3](https://img.shields.io/badge/Specification-v2.3-green.svg)](#)
[![Target Platform: FreeBSD 14+](https://img.shields.io/badge/Platform-FreeBSD_14+-red.svg)](#)
[![Status: Master Freeze](https://img.shields.io/badge/Status-Prior_Art_Standard-brightgreen.svg)](#)
## 1. Парадигма: Субъект против Периферии

ZAMICORE преодолевает фундаментальный порок современных LLM-систем — монополизацию интеллекта единой вероятностной нейросетью, страдающей от галлюцинаций, квадратичной сложности внимания и амнезии[cite: 1].

В ZAMICORE реализовано строгое кибернетическое разделение субъектности и вычислительных органов[cite: 1]:

* **Субъект интеллекта (Ядро ZAMICORE):** Детерминированная операционная среда под управлением FreeBSD 15.1[cite: 1]. Хранит память в неизменяемых 4KB-стратах OpenZFS (Merkle-DAG), выполняет логику через конечный автомат (FSM) над 7 аппаратными примитивами и управляет сценариями посредством шитого байткода (Threaded Code)[cite: 1].
* **Периферийный сенсорно-речевой орган (Neural ALU):** Сменная языковая модель (базовый референс: Qwen2.5-0.5B, $d_{\text{model}} = 896$), препарированная на 12-м слое («Экватор»)[cite: 1]. Модель низведена до роли акустического кодека: сжатие текста в смысл на входе ($L_{0-11}$) и синтаксическая развертка на выходе ($L_{13-24}$)[cite: 1].

```text
                  ┌────────────────────────────────────────┐
                  │      ZAMICORE COGNITIVE CORE (OS)      │
                  │  Твердая память OpenZFS, онтология Q1, │
                  │  Merkle-DAG, FSM, 7 микро-примитивов   │
                  └───────────────────┬────────────────────┘
                                      │
                   [ ШИНА: КОГНИТИВНЫЙ БУФЕР КАДРА ] (zami_frame_t)
                   (Двойная буферизация Front/Back, Zero-Copy)
                                      │
  ════════════════════════════════════╪════════════════════════════════════
                     СТАНДАРТНЫЙ НЕЙРОСЕТЕВОЙ СОКЕТ (ABI)
  ════════════════════════════════════╪════════════════════════════════════
                                      │
          ┌───────────────────────────┴───────────────────────────┐
          ▼                                                       ▼
  [ СЕНСОРНЫЙ РУПОР ]                                     [ РЕЧЕВОЙ РЕЗОНАТОР ]
  Слои 0..11 трансформера                                Слои 13..24 трансформера
  Сжатие потока токенов в u_raw                           Синтаксический спуск к EOS
          │                                                       ▲
          └───────────────────────────┬───────────────────────────┘
                                      │
                         [ ШЛЮЗ ЭКВАТОРА: СЛОЙ 12 ]
                         Инжекция устойчивого барицентра B
                         Пробник фазового напряжения Delta E

```

---

## 2. Физический фундамент: 4KB-страты на OpenZFS

Система полностью отказывается от хранения контекста в энергозависимой VRAM видеокарты (KV-кэш уничтожен)[cite: 1]. Единицей персистентной и оперативной памяти является **страт** размером ровно **4096 байт**, аппаратно выровненный под секторы NVMe Advanced Format 4Kn и параметр пула `ashift=12`[cite: 1, 2].

```text
  0x0000 ┌─────────────────────────────────────────────────────────────┐
         │ ЗАГОЛОВОК СТРАТА (64 байта):                                │
         │ • CRC32C заголовка (SSE4.2 _mm_crc32_u64, 4 байта)          │
         │ • Флаги квадранта и типа полезной нагрузки (4 байта)        │
         │ • Уникальный stratum_id, timestamp_ns, access_counter       │
         │ • SHA-256 Merkle Parent Hash (32 байта)                     │
  0x0040 ├─────────────────────────────────────────────────────────────┤
         │ ПОЛЕЗНАЯ НАГРУЗКА СТРАТА (4032 байта, строго типизирована): │
         │                                                             │
         │ [FLAG_CONCEPT]  ──► Смысловой Хаб (Барицентры и мереология) │
         │ [FLAG_ACTION]   ──► Граф Действий (Массив опкодов сценария) │
         │ [FLAG_ANALYTIC] ──► Аналитический узел (Proof Graph)        │
         │ [FLAG_GESTALT]  ──► Мета-Индекс (Bounding Volume R^896)     │
  0x1000 └─────────────────────────────────────────────────────────────┘ (Ровно 4096 байт)

```

* **Zero-Copy I/O:** Чтение и запись ведутся через системные вызовы `pread(2)` и `pwrite(2)` с флагом `O_DIRECT`, полностью минуя страничный кэш ядра FreeBSD[cite: 1, 2].
* **Аппаратная целостность:** Первые 56 байт заголовка валидируются инструкцией SSE4.2 `_mm_crc32_u64` ровно за 7 процессорных инструкций (~10 нс)[cite: 1, 2].

---

## 3. Четырехквадрантная организация памяти

| Квадрант | Физический носитель | Политика доступа | Назначение |
| --- | --- | --- | --- |
| **Q1 (ZAMI Core)** | OpenZFS (`zroot/zami/core`) | Read-Only при диалоге. Атомарный CoW-коммит[cite: 1]. | Объективные инварианты, семантические хабы, проверенные сценарии действий[cite: 1]. |
| **Q2 (User Space)** | OpenZFS (`zroot/zami/user`) | Append-Only сессионные срезы[cite: 1]. | Профиль пользователя, его персональный тезаурус и контекстные вехи[cite: 1]. |
| **Q3 (Scratchpad)** | DRAM / Выровненный SHM | Zero-Copy кольцевой буфер[cite: 1]. | Векторные проекции, расчет напряжений $\Delta E$, теневые кадры[cite: 1]. |
| **Q4 (Session)** | DRAM Ring Buffer | Экспоненциальное затухание (Decay)[cite: 1]. | Сырой речевой поток текущего диалога, стираемый при закрытии сессии[cite: 1]. |

---

## 4. Семь примитивов ядра и шитый код (Threaded Code)

В ZAMICORE категорически исключена генерация и компиляция исполняемого кода в рантайме (Python/Shell/C)[cite: 1]. Вся поведенческая вариативность сводится к исполнению сценариев из неизменяемых C-операндов единого ABI через конечный автомат (FSM)[cite: 1].

### Каталог элементарных примитивов ядра:

1. **`PRIM_STRATUM_IO`**: Атомарный прямой ввод-вывод 4096 байт с диска без кэша ядра (`pwrite`/`pread`, `O_DIRECT`)[cite: 1, 2].
2. **`PRIM_SEAL_CRC`**: Аппаратная проверка/печать целостности заголовка (SSE4.2 CRC32C, ~10 нс)[cite: 1, 2].
3. **`PRIM_VEC_METRIC`**: Скалярное произведение и косинусное сходство векторов в $\mathbb{R}^{896}$ (AVX2 + FMA, ~30 нс)[cite: 1].
4. **`PRIM_BARYCENTER`**: Потоковый расчет смыслового центра массы токенов на Экваторе (AVX2 streaming)[cite: 1].
5. **`PRIM_TENSION`**: Замер фазового сопротивления весов модели $\Delta E = \lVert \vec{u}_{\text{settled}} - \vec{u}_{\text{target}} \rVert_2$[cite: 1].
6. **`PRIM_FRAME_FLIP`**: Атомарное переключение теневого кадра в активный (`stdatomic`)[cite: 1].
7. **`PRIM_GRAPH_HOP`**: Прямой переход по 64-битному адресу `stratum_id` в памяти[cite: 1, 2].

> **Статус DDE (Delay Differential Equations):** Экспериментальный дифференциальный контур динамики временно отключен (`#define ZAMI_ENABLE_DDE 0`) и изолирован через no-op байпас до завершения калибровки прямого I/O и пробников натяжения на Фазе 1.

Сценарии кодируются плоскими массивами 16-битных идентификаторов (`uint16_t opcodes[]`) в 4KB-стратах и исполняются простым циклом обхода таблицы диспетчеризации[cite: 1]:

```c
for (size_t i = 0; i < num_ops; ++i) {
    zami_op_result_t res = registry->dispatch_table[opcodes[i]](frame, runtime_ctx);
    if (!res.success) return abort_scenario(res); /* Fail-Fast */
}

```

---

## 5. Эпистемический иммунитет и «Цепочки НАДО»

Система защищена от галлюцинаций, токсичности и саморазрушения тремя рубежами[cite: 1]:

* **Шлюз Экватора ($L_{12} \to L_{13}$):** Если инжектированный вектор противоречит логике, пробник $T_2$ фиксирует скачок напряжения $\Delta E \ge \tau_{\text{tension}}$[cite: 1]. Кадр немедленно сбрасывается (`dropped_frames++`), резонатор блокируется, а операция записи на диск отменяется[cite: 1].
* **Глобальное НАДО (Гомеостаз):** Любое действие проверяется проекцией на инвариант выживания системы $\vec{\mathcal{H}}$[cite: 1]. При выходе за конус допустимых параметров срабатывает аппаратное вето `ZAMI_ACTION_VETO`[cite: 1].
* **Операционные интерлоки:** Запись любого блока на диск предваряется линейной цепочкой безусловных микро-проверок[cite: 1]:

```mermaid
flowchart LR
    A[MUST_ISOLATE] --> B[MUST_RECALC_AVX2] --> C[MUST_TEST_TENSION] --> D[MUST_CHECK_HOMEO] --> E[MUST_SEAL_CRC32] --> F[(COMMIT)]

```

---

## 6. Сборка и развертывание

### Системные требования:

* **ОС:** FreeBSD 15.1-RELEASE (amd64)[cite: 1].
* **Накопитель:** NVMe SSD с пулом OpenZFS (`ashift=12`, `recordsize=4k`, `atime=off`)[cite: 1].
* **Процессор:** x86_64 с обязательной поддержкой SSE4.2, AVX2 и FMA3[cite: 1].
* **Компилятор:** Clang 18+ (C11 standard)[cite: 1].

### Компиляция ядра:

```bash
# Клонирование репозитория
git clone https://github.com/your-org/zamicore.git
cd zamicore

# Проверка бинарного контракта 4KB-страта
cc -std=c11 -Wall -Wextra -Werror -msse4.2 -mavx2 -mfma \
   -fno-fast-math -ffp-contract=off \
   -c src/core/zamicore_stratum.c -o build/zamicore_stratum.o

# Запуск тестов аппаратного I/O
make test

```

---

## 7. Цитирование (Citation)

При использовании архитектурных решений, структур данных 4KB-стратов или математического аппарата ZAMICORE в исследованиях и публикациях используйте следующую форму фиксации:

```bibtex
@software{zamicore2026,
  author       = {ZAMICORE Contributors},
  title        = {ZAMICORE: Deterministic Cognitive Operating System on FreeBSD and OpenZFS},
  year         = {2026},
  publisher    = {Zenodo},
  doi          = {10.5281/zenodo.22313830},
  url          = {https://doi.org/10.5281/zenodo.22313830}
}

```

---

## 8. Лицензия (License)

Проект распространяется под лицензией **BSD 2-Clause "Simplified" License**:

```text
Copyright (c) 2026, ZAMICORE Contributors. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

```
