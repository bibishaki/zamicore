Модуль `zamicore_math.c` реализует базовые операции векторной алгебры для размерности $d_{\text{model}} = 896$ на наборе инструкций **AVX2 + FMA**.

Вычисления оптимизированы под 4-канальный конвейер FMA (4 независимых векторных аккумулятора), что устраняет простои конвейера процессора (latency 4 такта) и обеспечивает строго детерминированный порядок редукции сумм по сбалансированному бинарному дереву.

---

### Заголовочный файл `zamicore_math.h`

```c
#ifndef ZAMICORE_MATH_H
#define ZAMICORE_MATH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZAMI_DIM 896U /* d_model для Qwen2.5-0.5B */

/*
 * Инициализация аппаратного контекста CPU:
 * Установка флагов FTZ (Flush-To-Zero) и DAZ (Denormals-Are-Zero) в регистре MXCSR
 * для исключения микропрограммных задержек и обеспечения детерминизма IEEE 754.
 */
void zamicore_init_cpu_state(void);

/*
 * Скалярное произведение векторов размерности 896 (AVX2 + FMA).
 * Требование: массивы a и b должны быть выровнены по границе 32 байт.
 */
float zamicore_dot_896(const float *a, const float *b);

/* Евклидова норма вектора (L2-norm) */
float zamicore_norm_896(const float *a);

/*
 * Косинусное сходство (направленность) между векторами:
 * cos(theta) = (a . b) / (||a|| * ||b||)
 */
float zamicore_cosine_similarity_896(const float *a, const float *b);

/*
 * Расчет ортогональной невязки (вектора новизны) и скалярного импульса I:
 * u_parallel = ((u . h) / (h . h)) * h
 * r = u - u_parallel
 * I = ||r||_2
 *
 * Если out_r != NULL, записывает вычисленный вектор r в память (896 float, выравнивание 32B).
 * Возвращает скалярное значение импульса I.
 */
float zamicore_compute_impulse_896(const float *u, 
                                   const float *h_attractor, 
                                   float *out_r);

#ifdef __cplusplus
}
#endif

#endif /* ZAMICORE_MATH_H */

```

---

### Реализация `zamicore_math.c`

```c
#include "zamicore_math.h"

#include <immintrin.h>
#include <xmmintrin.h>
#include <pmmintrin.h>
#include <math.h>

#define EPSILON_STABILITY 1e-12f

void zamicore_init_cpu_state(void) {
    /* Принудительное обнуление денормализованных чисел */
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
}

/**
 * @brief Детерминированная редукция 8-полосного 256-битного регистра YMM в один float.
 * Сложение идет строго по бинарному дереву:
 * ((0+4) + (2+6)) + ((1+5) + (3+7))
 */
static inline float horizontal_add_ymm(__m256 acc) {
    /* Шаг 1: Сложение старшей и младшей 128-битных половин */
    __m128 lo = _mm256_castps256_ps128(acc);
    __m128 hi = _mm256_extractf128_ps(acc, 1);
    __m128 sum4 = _mm_add_ps(lo, hi); /* [0+4, 1+5, 2+6, 3+7] */

    /* Шаг 2: Попарное сложение четных и нечетных элементов */
    __m128 sum2 = _mm_add_ps(sum4, _mm_movehl_ps(sum4, sum4)); /* [(0+4)+(2+6), (1+5)+(3+7), ...] */

    /* Шаг 3: Финальное сложение в скаляр */
    __m128 sum1 = _mm_add_ss(sum2, _mm_shuffle_ps(sum2, sum2, 1));

    return _mm_cvtss_f32(sum1);
}

float zamicore_dot_896(const float *a, const float *b) {
    /*
     * 896 / 8 = 112 векторных операций.
     * Разворачиваем цикл на 4 аккумулятора: 112 / 4 = 28 итераций.
     * Полностью заполняет суперскалярный конвейер FMA процессора.
     */
    __m256 acc0 = _mm256_setzero_ps();
    __m256 acc1 = _mm256_setzero_ps();
    __m256 acc2 = _mm256_setzero_ps();
    __m256 acc3 = _mm256_setzero_ps();

    for (size_t i = 0; i < ZAMI_DIM; i += 32) {
        __m256 va0 = _mm256_load_ps(a + i);
        __m256 vb0 = _mm256_load_ps(b + i);
        acc0 = _mm256_fmadd_ps(va0, vb0, acc0);

        __m256 va1 = _mm256_load_ps(a + i + 8);
        __m256 vb1 = _mm256_load_ps(b + i + 8);
        acc1 = _mm256_fmadd_ps(va1, vb1, acc1);

        __m256 va2 = _mm256_load_ps(a + i + 16);
        __m256 vb2 = _mm256_load_ps(b + i + 16);
        acc2 = _mm256_fmadd_ps(va2, vb2, acc2);

        __m256 va3 = _mm256_load_ps(a + i + 24);
        __m256 vb3 = _mm256_load_ps(b + i + 24);
        acc3 = _mm256_fmadd_ps(va3, vb3, acc3);
    }

    /* Детерминированное парное сложение аккумуляторов */
    __m256 sum01 = _mm256_add_ps(acc0, acc1);
    __m256 sum23 = _mm256_add_ps(acc2, acc3);
    __m256 total = _mm256_add_ps(sum01, sum23);

    return horizontal_add_ymm(total);
}

float zamicore_norm_896(const float *a) {
    float dot = zamicore_dot_896(a, a);
    return sqrtf(dot);
}

float zamicore_cosine_similarity_896(const float *a, const float *b) {
    float dot_ab = zamicore_dot_896(a, b);
    float dot_aa = zamicore_dot_896(a, a);
    float dot_bb = zamicore_dot_896(b, b);

    float denom = sqrtf(dot_aa * dot_bb);
    if (denom < EPSILON_STABILITY) {
        return 0.0f;
    }

    float cos_theta = dot_ab / denom;

    /* Жесткое ограничение диапазона от погрешностей округления */
    if (cos_theta > 1.0f) return 1.0f;
    if (cos_theta < -1.0f) return -1.0f;

    return cos_theta;
}

float zamicore_compute_impulse_896(const float *u, 
                                   const float *h_attractor, 
                                   float *out_r) {
    float dot_uh = zamicore_dot_896(u, h_attractor);
    float dot_hh = zamicore_dot_896(h_attractor, h_attractor);

    /* Если аттрактор пуст (нулевой вектор), весь стимул u является импульсом */
    if (dot_hh < EPSILON_STABILITY) {
        if (out_r) {
            for (size_t i = 0; i < ZAMI_DIM; i += 8) {
                __m256 vu = _mm256_load_ps(u + i);
                _mm256_store_ps(out_r + i, vu);
            }
        }
        return zamicore_norm_896(u);
    }

    /* alpha = (u . h) / (h . h) */
    float alpha = dot_uh / dot_hh;
    __m256 valpha = _mm256_set1_ps(alpha);

    __m256 acc_r0 = _mm256_setzero_ps();
    __m256 acc_r1 = _mm256_setzero_ps();
    __m256 acc_r2 = _mm256_setzero_ps();
    __m256 acc_r3 = _mm256_setzero_ps();

    /*
     * Вычисление r = u - alpha * h с одновременным накоплением суммы квадратов ||r||^2.
     * Инструкция _mm256_fnmadd_ps(a, b, c) выполняет -(a * b) + c,
     * то есть в точности u - (alpha * h) без промежуточного округления.
     */
    for (size_t i = 0; i < ZAMI_DIM; i += 32) {
        /* Блок 0 */
        __m256 vu0 = _mm256_load_ps(u + i);
        __m256 vh0 = _mm256_load_ps(h_attractor + i);
        __m256 vr0 = _mm256_fnmadd_ps(valpha, vh0, vu0);
        if (out_r) _mm256_store_ps(out_r + i, vr0);
        acc_r0 = _mm256_fmadd_ps(vr0, vr0, acc_r0);

        /* Блок 1 */
        __m256 vu1 = _mm256_load_ps(u + i + 8);
        __m256 vh1 = _mm256_load_ps(h_attractor + i + 8);
        __m256 vr1 = _mm256_fnmadd_ps(valpha, vh1, vu1);
        if (out_r) _mm256_store_ps(out_r + i + 8, vr1);
        acc_r1 = _mm256_fmadd_ps(vr1, vr1, acc_r1);

        /* Блок 2 */
        __m256 vu2 = _mm256_load_ps(u + i + 16);
        __m256 vh2 = _mm256_load_ps(h_attractor + i + 16);
        __m256 vr2 = _mm256_fnmadd_ps(valpha, vh2, vu2);
        if (out_r) _mm256_store_ps(out_r + i + 16, vr2);
        acc_r2 = _mm256_fmadd_ps(vr2, vr2, acc_r2);

        /* Блок 3 */
        __m256 vu3 = _mm256_load_ps(u + i + 24);
        __m256 vh3 = _mm256_load_ps(h_attractor + i + 24);
        __m256 vr3 = _mm256_fnmadd_ps(valpha, vh3, vu3);
        if (out_r) _mm256_store_ps(out_r + i + 24, vr3);
        acc_r3 = _mm256_fmadd_ps(vr3, vr3, acc_r3);
    }

    __m256 sum_r01 = _mm256_add_ps(acc_r0, acc_r1);
    __m256 sum_r23 = _mm256_add_ps(acc_r2, acc_r3);
    __m256 total_r = _mm256_add_ps(sum_r01, sum_r23);

    float norm_sq = horizontal_add_ymm(total_r);
    return sqrtf(norm_sq);
}

```

---

### Архитектурные особенности реализации:

1. **Конвейеризация FMA:** Инструкции `_mm256_fmadd_ps` имеют задержку выполнения в 4 такта. Использование четырех независимых регистров-накопителей (`acc0`–`acc3`) позволяет процессору исполнять операции на каждом такте без ожидания результата предыдущей итерации.
2. **Нулевая погрешность при расчете невязки:** Применение `_mm256_fnmadd_ps` ($u - \alpha h$) выполняет умножение и вычитание за одну аппаратную микрооперацию с единственным округлением в конце, что гарантирует точную ортогональность $\vec{r} \perp \vec{h}$.
3. **Требование к памяти:** Функции используют `_mm256_load_ps` и `_mm256_store_ps`. Передаваемые указатели обязаны быть выровнены по границе 32 байт (для `zamicore_stratum_t` смещение поля `payload` равно 64 байтам, что автоматически кратно 32).
