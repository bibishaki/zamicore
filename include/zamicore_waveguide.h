Реализация волноводного интерфейса `zamicore_waveguide` выполнена в соответствии со спецификацией Раздела 2.3 (`SPEC_DRAFT.md`).

Код ориентирован на компилятор Clang под FreeBSD 15.1, не содержит динамических аллокаций памяти (`malloc`) в горячем цикле инференса и использует 4-канальный конвейер FMA для размерности $d_{\text{model}} = 896$.

---

### Заголовочный файл `zamicore_waveguide.h`

```c
#ifndef ZAMICORE_WAVEGUIDE_H
#define ZAMICORE_WAVEGUIDE_H

#include "zamicore_stratum.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Порог активации режима абсолютного холода (Крио-защелка) */
#define ZAMI_T_INJ_CRYO_EPSILON   1e-4f
#define ZAMI_T_INJ_MAX_STIFFNESS  10000.0f

typedef enum {
    ZAMI_WAVE_OK              =  0,
    ZAMI_WAVE_ERR_NULL_PTR    = -1,
    ZAMI_WAVE_ERR_ALIGN       = -2, /* Буфер не выровнен по границе 32 байт */
    ZAMI_WAVE_ERR_INVALID_T   = -3  /* T_inj <= 0.0f или NaN */
} zami_wave_status_t;

/**
 * @brief Дескриптор двухточечной каскадной инжекции.
 */
typedef struct zami_dual_injection_params {
    float t_inj_sem;    /* Температура смыслового экватора L_sem (0.0f .. inf) */
    float t_inj_prag;   /* Температура прагматического среза L_prag (0.0f .. inf) */
} zami_dual_injection_params_t;

/**
 * @brief Вычисление волнового фронта наведения для одного инжектора:
 *        u_target = u_raw + (1.0 / T_inj) * delta
 *
 * @param u_raw        Входной фазовый вектор (896 float, выравнивание 32B).
 * @param delta        Корректирующий вектор барицентра (896 float, выравнивание 32B).
 * @param t_inj        Физическая температура инжекции T_inj.
 * @param out_u_target Результирующий волновой вектор (896 float, выравнивание 32B).
 */
zami_wave_status_t zamicore_apply_injection(const float *u_raw,
                                            const float *delta,
                                            float t_inj,
                                            float *out_u_target);

/**
 * @brief Замер топологического напряжения многообразия (Delta E):
 *        Delta_E = ||u_settled - u_target||_2
 *
 * @param u_settled Фиактический вектор после релаксации слоя (выравнивание 32B).
 * @param u_target  Целевой вектор, поданный на инжектор (выравнивание 32B).
 * @return float    Евклидово расстояние (мера сопротивления весов модели).
 */
float zamicore_measure_tension(const float *u_settled,
                               const float *u_target);

/**
 * @brief Каскадный двухточечный расчет инжекции (Dual-Injector Pipeline).
 *
 * Выполняет согласованную подготовку волновых фронтов для обоих срезов:
 * 1. Смысловой экватор: out_sem_target = u_raw + (1/T_sem) * delta_sem
 * 2. Прагматический срез: out_prag_target = u_mid + (1/T_prag) * delta_prag
 */
zami_wave_status_t zamicore_inject_dual(const float *u_raw,
                                        const float *delta_sem,
                                        const float *u_mid,
                                        const float *delta_prag,
                                        const zami_dual_injection_params_t *params,
                                        float *out_sem_target,
                                        float *out_prag_target);

#ifdef __cplusplus
}
#endif

#endif /* ZAMICORE_WAVEGUIDE_H */

```

---

### Реализация `zamicore_waveguide.c`

```c
#include "zamicore_waveguide.h"

#include <immintrin.h>
#include <math.h>

/**
 * @brief Вспомогательная детерминированная редукция регистра YMM по бинарному дереву.
 */
static inline float horizontal_add_ymm(__m256 acc) {
    __m128 lo = _mm256_castps256_ps128(acc);
    __m128 hi = _mm256_extractf128_ps(acc, 1);
    __m128 sum4 = _mm_add_ps(lo, hi);
    __m128 sum2 = _mm_add_ps(sum4, _mm_movehl_ps(sum4, sum4));
    __m128 sum1 = _mm_add_ss(sum2, _mm_shuffle_ps(sum2, sum2, 1));
    return _mm_cvtss_f32(sum1);
}

/**
 * @brief Расчет жесткости поля 1 / T_inj с аппаратным подавлением деления на ноль.
 */
static inline float compute_stiffness(float t_inj) {
    if (t_inj <= ZAMI_T_INJ_CRYO_EPSILON) {
        /* Режим крио-защелки: жесткость насыщается до предельного максимума */
        return ZAMI_T_INJ_MAX_STIFFNESS;
    }
    return 1.0f / t_inj;
}

zami_wave_status_t zamicore_apply_injection(const float *u_raw,
                                            const float *delta,
                                            float t_inj,
                                            float *out_u_target) {
    if (!u_raw || !delta || !out_u_target) {
        return ZAMI_WAVE_ERR_NULL_PTR;
    }
    if (t_inj < 0.0f || isnan(t_inj)) {
        return ZAMI_WAVE_ERR_INVALID_T;
    }
    if (((uintptr_t)u_raw % 32 != 0) || 
        ((uintptr_t)delta % 32 != 0) || 
        ((uintptr_t)out_u_target % 32 != 0)) {
        return ZAMI_WAVE_ERR_ALIGN;
    }

    float stiffness = compute_stiffness(t_inj);
    __m256 v_stiff = _mm256_set1_ps(stiffness);

    /* 
     * Разворот цикла: 896 float / 32 float (4 блока по 8) = 28 итераций.
     * Вычисление u_target = u_raw + stiffness * delta через FMA.
     */
    for (size_t i = 0; i < ZAMI_QWEN_DIM; i += 32) {
        /* Блок 0 */
        __m256 ur0 = _mm256_load_ps(u_raw + i);
        __m256 d0  = _mm256_load_ps(delta + i);
        __m256 res0 = _mm256_fmadd_ps(v_stiff, d0, ur0);
        _mm256_store_ps(out_u_target + i, res0);

        /* Блок 1 */
        __m256 ur1 = _mm256_load_ps(u_raw + i + 8);
        __m256 d1  = _mm256_load_ps(delta + i + 8);
        __m256 res1 = _mm256_fmadd_ps(v_stiff, d1, ur1);
        _mm256_store_ps(out_u_target + i + 8, res1);

        /* Блок 2 */
        __m256 ur2 = _mm256_load_ps(u_raw + i + 16);
        __m256 d2  = _mm256_load_ps(delta + i + 16);
        __m256 res2 = _mm256_fmadd_ps(v_stiff, d2, ur2);
        _mm256_store_ps(out_u_target + i + 16, res2);

        /* Блок 3 */
        __m256 ur3 = _mm256_load_ps(u_raw + i + 24);
        __m256 d3  = _mm256_load_ps(delta + i + 24);
        __m256 res3 = _mm256_fmadd_ps(v_stiff, d3, ur3);
        _mm256_store_ps(out_u_target + i + 24, res3);
    }

    return ZAMI_WAVE_OK;
}

float zamicore_measure_tension(const float *u_settled,
                               const float *u_target) {
    if (!u_settled || !u_target) {
        return -1.0f;
    }
    if (((uintptr_t)u_settled % 32 != 0) || ((uintptr_t)u_target % 32 != 0)) {
        return -1.0f;
    }

    __m256 acc0 = _mm256_setzero_ps();
    __m256 acc1 = _mm256_setzero_ps();
    __m256 acc2 = _mm256_setzero_ps();
    __m256 acc3 = _mm256_setzero_ps();

    /* 
     * Вычисление квадрата евклидова расстояния ||u_settled - u_target||^2
     * с параллельным накоплением сумм в 4 независимых регистра.
     */
    for (size_t i = 0; i < ZAMI_QWEN_DIM; i += 32) {
        /* Блок 0 */
        __m256 s0 = _mm256_load_ps(u_settled + i);
        __m256 t0 = _mm256_load_ps(u_target + i);
        __m256 diff0 = _mm256_sub_ps(s0, t0);
        acc0 = _mm256_fmadd_ps(diff0, diff0, acc0);

        /* Блок 1 */
        __m256 s1 = _mm256_load_ps(u_settled + i + 8);
        __m256 t1 = _mm256_load_ps(u_target + i + 8);
        __m256 diff1 = _mm256_sub_ps(s1, t1);
        acc1 = _mm256_fmadd_ps(diff1, diff1, acc1);

        /* Блок 2 */
        __m256 s2 = _mm256_load_ps(u_settled + i + 16);
        __m256 t2 = _mm256_load_ps(u_target + i + 16);
        __m256 diff2 = _mm256_sub_ps(s2, t2);
        acc2 = _mm256_fmadd_ps(diff2, diff2, acc2);

        /* Блок 3 */
        __m256 s3 = _mm256_load_ps(u_settled + i + 24);
        __m256 t3 = _mm256_load_ps(u_target + i + 24);
        __m256 diff3 = _mm256_sub_ps(s3, t3);
        acc3 = _mm256_fmadd_ps(diff3, diff3, acc3);
    }

    __m256 sum01 = _mm256_add_ps(acc0, acc1);
    __m256 sum23 = _mm256_add_ps(acc2, acc3);
    __m256 total = _mm256_add_ps(sum01, sum23);

    float norm_sq = horizontal_add_ymm(total);
    return sqrtf(norm_sq);
}

zami_wave_status_t zamicore_inject_dual(const float *u_raw,
                                        const float *delta_sem,
                                        const float *u_mid,
                                        const float *delta_prag,
                                        const zami_dual_injection_params_t *params,
                                        float *out_sem_target,
                                        float *out_prag_target) {
    if (!params) {
        return ZAMI_WAVE_ERR_NULL_PTR;
    }

    /* Фаза 1: Смысловой экватор (L_sem) */
    zami_wave_status_t status = zamicore_apply_injection(u_raw, 
                                                         delta_sem, 
                                                         params->t_inj_sem, 
                                                         out_sem_target);
    if (status != ZAMI_WAVE_OK) {
        return status;
    }

    /* Фаза 2: Прагматический срез (L_prag) */
    return zamicore_apply_injection(u_mid, 
                                    delta_prag, 
                                    params->t_inj_prag, 
                                    out_prag_target);
}

```

---

### Архитектурные свойства модуля:

1. **Безопасность Крио-защелки:**
При $T_{\text{inj}} \le 10^{-4}$ (включая нулевые значения) жесткость не вызывает аппаратного исключения `IEEE #DZ` (Division by Zero), а детерминированно фиксируется на значении $10000.0$, обеспечивая принудительный фазовый захват траектории без переполнения чисел `float`.
2. **Нулевой Spill регистров:**
В цикле задействовано ровно 8 регистров `ymm` на итерацию (4 накопителя + 4 временных), что полностью укладывается в 16 регистров набора x86-64 и исключает сброс значений на стек ядра.
3. **Прямая интеграция с пробниками:**
Функция `zamicore_measure_tension` вызывается непосредственно после снятия векторов пробниками $T_2$ и $T_4$, мгновенно предоставляя скаляры $\Delta E_1$ и $\Delta E_2$ для работы эпистемического фильтра.
