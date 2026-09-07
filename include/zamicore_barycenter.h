Функция расчета взвешенного барицентра реализована в соответствии со спецификацией Раздела 11 (`SPEC_DRAFT.md`).

Алгоритм использует потоковую модель вычислений: входной массив токенов считывается строго последовательно из памяти (активируя аппаратный *stream prefetcher* процессора), в то время как 896-мерный вектор-аккумулятор (всего 3584 байта) непрерывно удерживается в кэше первого уровня (L1D Cache). Накопление взвешенных компонент выполняется за 4 независимых FMA-конвейера (`_mm256_fmadd_ps`).

---

### Заголовочный интерфейс (`zamicore_barycenter.h`)

```c
#ifndef ZAMICORE_BARYCENTER_H
#define ZAMICORE_BARYCENTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZAMI_DIM 896U

typedef enum {
    ZAMI_BARY_OK           =  0,
    ZAMI_BARY_ERR_NULL_PTR = -1,
    ZAMI_BARY_ERR_ALIGN    = -2, /* Буфер не выровнен по границе 32 байт */
    ZAMI_BARY_ERR_EMPTY    = -3, /* num_tokens == 0 */
    ZAMI_BARY_ERR_ZERO_WT  = -4  /* Сумма весов токенов меньше допустимого порога */
} zami_bary_status_t;

/**
 * @brief Расчет взвешенного семантического барицентра массива скрытых векторов L_12.
 *
 * @param tokens        Непрерывный массив размером (num_tokens * 896) float (выравнивание 32B).
 * @param weights       Массив скалярных весов токенов размером num_tokens (если NULL, веса = 1.0f).
 * @param num_tokens    Количество токенов в блоке (M).
 * @param out_barycenter Выходной вектор размерности 896 float (выравнивание 32B).
 * @return zami_bary_status_t Статус выполнения операции.
 */
zami_bary_status_t zamicore_compute_barycenter(const float *tokens,
                                               const float *weights,
                                               size_t num_tokens,
                                               float *out_barycenter);

#ifdef __cplusplus
}
#endif

#endif /* ZAMICORE_BARYCENTER_H */

```

---

### Исходный код (`zamicore_barycenter.c`)

```c
#include "zamicore_barycenter.h"

#include <immintrin.h>
#include <string.h>

#define EPSILON_WEIGHT 1e-12f

zami_bary_status_t zamicore_compute_barycenter(const float *tokens,
                                               const float *weights,
                                               size_t num_tokens,
                                               float *out_barycenter) {
    /* 1. Валидация входных указателей и параметров */
    if (!tokens || !out_barycenter) {
        return ZAMI_BARY_ERR_NULL_PTR;
    }
    if (num_tokens == 0) {
        return ZAMI_BARY_ERR_EMPTY;
    }

    /* Проверка 32-байтного выравнивания для безопасного AVX2-доступа */
    if (((uintptr_t)tokens % 32 != 0) || ((uintptr_t)out_barycenter % 32 != 0)) {
        return ZAMI_BARY_ERR_ALIGN;
    }

    /* 2. Подсчет суммарной массы блока */
    float sum_weights = 0.0f;
    if (weights) {
        for (size_t i = 0; i < num_tokens; ++i) {
            sum_weights += weights[i];
        }
        if (sum_weights < EPSILON_WEIGHT) {
            return ZAMI_BARY_ERR_ZERO_WT;
        }
    } else {
        sum_weights = (float)num_tokens;
    }

    /* 3. Очистка аккумулятора барицентра в L1-кэше (3584 байта) */
    __m256 vzero = _mm256_setzero_ps();
    for (size_t d = 0; d < ZAMI_DIM; d += 32) {
        _mm256_store_ps(out_barycenter + d,      vzero);
        _mm256_store_ps(out_barycenter + d + 8,  vzero);
        _mm256_store_ps(out_barycenter + d + 16, vzero);
        _mm256_store_ps(out_barycenter + d + 24, vzero);
    }

    /* 
     * 4. Потоковое FMA-накопление:
     * Внешний цикл проходит по токенам последовательно (Linear Streaming).
     * Внутренний цикл развернут на 4 векторных блока (32 float = 128 байт за шаг).
     */
    for (size_t i = 0; i < num_tokens; ++i) {
        const float *tok_vec = tokens + (i * ZAMI_DIM);
        float w = weights ? weights[i] : 1.0f;

        /* Если вес токена ничтожно мал, пропускаем расчет вектора */
        if (w < EPSILON_WEIGHT) {
            continue;
        }

        __m256 vw = _mm256_set1_ps(w);

        for (size_t d = 0; d < ZAMI_DIM; d += 32) {
            /* Блок 0 */
            __m256 vt0 = _mm256_load_ps(tok_vec + d);
            __m256 va0 = _mm256_load_ps(out_barycenter + d);
            va0 = _mm256_fmadd_ps(vw, vt0, va0);
            _mm256_store_ps(out_barycenter + d, va0);

            /* Блок 1 */
            __m256 vt1 = _mm256_load_ps(tok_vec + d + 8);
            __m256 va1 = _mm256_load_ps(out_barycenter + d + 8);
            va1 = _mm256_fmadd_ps(vw, vt1, va1);
            _mm256_store_ps(out_barycenter + d + 8, va1);

            /* Блок 2 */
            __m256 vt2 = _mm256_load_ps(tok_vec + d + 16);
            __m256 va2 = _mm256_load_ps(out_barycenter + d + 16);
            va2 = _mm256_fmadd_ps(vw, vt2, va2);
            _mm256_store_ps(out_barycenter + d + 16, va2);

            /* Блок 3 */
            __m256 vt3 = _mm256_load_ps(tok_vec + d + 24);
            __m256 va3 = _mm256_load_ps(out_barycenter + d + 24);
            va3 = _mm256_fmadd_ps(vw, vt3, va3);
            _mm256_store_ps(out_barycenter + d + 24, va3);
        }
    }

    /* 
     * 5. Финальная нормализация на суммарную массу:
     * Деление заменяется на умножение на обратную величину inv_sum (O(1)).
     */
    float inv_sum = 1.0f / sum_weights;
    __m256 vinv = _mm256_set1_ps(inv_sum);

    for (size_t d = 0; d < ZAMI_DIM; d += 32) {
        __m256 b0 = _mm256_load_ps(out_barycenter + d);
        __m256 b1 = _mm256_load_ps(out_barycenter + d + 8);
        __m256 b2 = _mm256_load_ps(out_barycenter + d + 16);
        __m256 b3 = _mm256_load_ps(out_barycenter + d + 24);

        b0 = _mm256_mul_ps(b0, vinv);
        b1 = _mm256_mul_ps(b1, vinv);
        b2 = _mm256_mul_ps(b2, vinv);
        b3 = _mm256_mul_ps(b3, vinv);

        _mm256_store_ps(out_barycenter + d, b0);
        _mm256_store_ps(out_barycenter + d + 8, b1);
        _mm256_store_ps(out_barycenter + d + 16, b2);
        _mm256_store_ps(out_barycenter + d + 24, b3);
    }

    return ZAMI_BARY_OK;
}

```

---

### Архитектурные свойства и оптимизация под железо

1. **Линейная шина чтения (Data Locality):**
При обработке блока из 256 токенов ($256 \times 896 \times 4 \text{ байта} \approx 917 \text{ КБ}$) данные читаются строго последовательно по адресам памяти. Процессорные линии предвыборки (L2 Stream Prefetchers) подтягивают следующие кэш-линии задолго до вызова инструкции `_mm256_load_ps`, сводя задержки памяти к минимуму.
2. **Нулевой Cache Thrashing:**
Буфер `out_barycenter` занимает ровно **3584 байта** (56 кэш-линий по 64 байта). Он полностью помещается в стандартный L1 Data Cache (32–48 КБ) и не вытесняется на протяжении всего времени работы цикла.
3. **Эргономика вызова:**
Передача `weights = NULL` переключает расчет в режим равновесного геометрического центроида $\frac{1}{M}\sum \vec{h}_i$, избавляя от необходимости аллоцировать массив единиц.
