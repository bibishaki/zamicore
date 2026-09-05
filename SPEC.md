# ZAMICORE System Specification (v0.1-draft)
Deterministic State-Vector Storage & Inference Architecture

## 1. System Invariants (Базовые принципы)
1. Vector Anchor Over Token History: Контекст диалога редуцируется до единичного вектора скрытого состояния h ∈ S^(d-1) вместо конкатенации KV-кэша.
2. Zero-VRAM Contamination: Видеопамять освобождается после генерации ответа. Нет накопления истории — нет семантического дрейфа и энтропии внимания.
3. Deterministic Tunneling: Вектор инжектируется напрямую в срез слоев (экватор). При смене темы — холодный чистый старт без старых токенов.

---

## 2. Hardware & Storage Layout (ZFS & NVMe)

### 2.1 Dataset Parameters (FreeBSD ZFS)
* Root Dataset: zroot/zamicore
* Attractors Dataset (реестр долговременных якорей):
  - recordsize = 8k (ровно float16[4096] = 8192 байта)
  - compression = off (энтропия нормализованных тензоров ~ белый шум)
  - atime = off (ликвидация паразитного I/O на запись)
  - primarycache = all (принудительное удержание в ARC/ОЗУ)
  - secondarycache = all (L2ARC на быстром NVMe)
  - sync = standard, logbias = latency
* Scratchpad Dataset (оперативный кольцевой буфер кинетики):
  - sync = disabled (скорость системной памяти, обход ZIL)
  - redundant_metadata = most

### 2.2 Hierarchical Memory Mapping (4KB Alignment)
* Physical Page Size: 4096 байт (аппаратный сектор NVMe).
* Micro-Routing Density: 32 бассейна на 1 страницу ZFS (128 байт/бассейн: низкоранговые проекции d_sub=64 в fp16).
* I/O Guarantee: One-Shot 4 KB read per routing step.

---

## 3. Math & Vector Engine (AVX-512 SIMD)

### 3.1 Dimensions & Types
* Dimension (d_model): 4096 (для моделей 7B/8B) или 8192 (для 70B).
* Precision: float16 для хранения на диске (8 КБ), float32 в регистрах AVX-512 (zmm0-zmm31).
* Constraints: Строгое выравнивание в памяти по 64 байтам (aligned_alloc(64, ...)).
* Dependency Policy: Zero external math libraries (BLAS/LAPACK free).

### 3.2 State Transitions
* Внутриканьонный сдвиг (NLERP):
  h_{t+1} = normalize((1 - α) * h_t + α * u)
* Детекция бифуркации (смена темы):
  - Условие: cos(h_t, u) < THRESHOLD_BIFURCATION (default: ~0.35)
  - Действие: h_t коммитится в снапшот ZFS, VRAM очищается, u становится новым корнем h_0.
* Разрешение парадоксов (Saddle Points):
  - Метод: Gentlest Ascent Dynamics (GAD) на единичной гиперсфере.
  - Поведение: Заморозка перевала в узел BIFURCATION_SADDLE или ортогональный импульс e_orth (синтез).

---

## 4. Tensor Injection Pipeline

### 4.1 Layer Cut-Off (Экватор)
* Точка среза (L_eq): ~60-65% глубины модели (слои 21-24 для 32-слойной архитектуры).
* Процедура калибровки: Индивидуальный эмпирический замер дисперсии скрытых состояний.
* Пропуск прямого прохода: 65% ранних слоев исключаются из инференса при попадании в каньон.

### 4.2 Cross-Attention as Disk Navigation
* Macro-Routing: α_trunk = softmax( (h * C_trunks^T) / sqrt(d) )
* Meso-Routing:  α_branch = softmax( (h * C_branches^T) / sqrt(d) )
* Micro-Routing: α_basin = softmax( (h * C_page32^T) / sqrt(d) ) [Single 4KB Page]
* Сложность выборки: O(log N) от объема сохраненных знаний.
