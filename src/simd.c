#include "mesaGL/simd.h"

#include <string.h>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
static __m128 load_rgba(const unsigned char *pixel)
{
    unsigned packed;
    __m128i bytes;
    __m128i words;

    memcpy(&packed, pixel, sizeof(packed));
    bytes = _mm_cvtsi32_si128((int)packed);
    words = _mm_unpacklo_epi8(bytes, _mm_setzero_si128());
    return _mm_cvtepi32_ps(_mm_unpacklo_epi16(words, _mm_setzero_si128()));
}

static int linear_rgba8888_to_xrgb8888_sse2(
    void *user, unsigned char *destination,
    const unsigned char *source_row0, const unsigned char *source_row1,
    const NTGLlinearColumn *columns, int count, float row_alpha);

#if defined(__GNUC__) || defined(__clang__)
#define MESAGL_TARGET_AVX2 __attribute__((target("avx2")))

MESAGL_TARGET_AVX2
static __m256 load_two_rgba(const unsigned char *pixel0,
                            const unsigned char *pixel1)
{
    __m256 value = _mm256_castps128_ps256(load_rgba(pixel0));

    return _mm256_insertf128_ps(value, load_rgba(pixel1), 1);
}

MESAGL_TARGET_AVX2
static int linear_rgba8888_to_xrgb8888_avx2(
    void *user, unsigned char *destination,
    const unsigned char *source_row0, const unsigned char *source_row1,
    const NTGLlinearColumn *columns, int count, float row_alpha)
{
    const __m256 half = _mm256_set1_ps(0.5f);
    const __m256 row_weight = _mm256_set1_ps(row_alpha);
    const __m256 inverse_row = _mm256_set1_ps(1.0f - row_alpha);
    int index;

    (void)user;
    for (index = 0; index + 1 < count; index += 2) {
        __m256 column_weight = _mm256_set_m128(
            _mm_set1_ps(columns[index + 1].alpha),
            _mm_set1_ps(columns[index].alpha));
        __m256 inverse_column = _mm256_sub_ps(
            _mm256_set1_ps(1.0f), column_weight);
        __m256 top = _mm256_add_ps(
            _mm256_mul_ps(load_two_rgba(
                source_row0 + columns[index].x0 * 4,
                source_row0 + columns[index + 1].x0 * 4), inverse_column),
            _mm256_mul_ps(load_two_rgba(
                source_row0 + columns[index].x1 * 4,
                source_row0 + columns[index + 1].x1 * 4), column_weight));
        __m256 bottom = _mm256_add_ps(
            _mm256_mul_ps(load_two_rgba(
                source_row1 + columns[index].x0 * 4,
                source_row1 + columns[index + 1].x0 * 4), inverse_column),
            _mm256_mul_ps(load_two_rgba(
                source_row1 + columns[index].x1 * 4,
                source_row1 + columns[index + 1].x1 * 4), column_weight));
        __m256 value = _mm256_add_ps(
            _mm256_mul_ps(top, inverse_row),
            _mm256_mul_ps(bottom, row_weight));
        __m256i integer;
        int rgba[8];
        int lane;

        integer = _mm256_cvttps_epi32(_mm256_add_ps(value, half));
        _mm256_storeu_si256((__m256i *)rgba, integer);
        for (lane = 0; lane < 2; ++lane) {
            destination[(index + lane) * 4 + 0] =
                (unsigned char)rgba[lane * 4 + 2];
            destination[(index + lane) * 4 + 1] =
                (unsigned char)rgba[lane * 4 + 1];
            destination[(index + lane) * 4 + 2] =
                (unsigned char)rgba[lane * 4 + 0];
            destination[(index + lane) * 4 + 3] = 255;
        }
    }

    if (index < count) {
        return linear_rgba8888_to_xrgb8888_sse2(
            user, destination + index * 4, source_row0, source_row1,
            columns + index, 1, row_alpha);
    }
    return 1;
}
#endif

static int linear_rgba8888_to_xrgb8888_sse2(
    void *user, unsigned char *destination,
    const unsigned char *source_row0, const unsigned char *source_row1,
    const NTGLlinearColumn *columns, int count, float row_alpha)
{
    const __m128 scale_down = _mm_set1_ps(1.0f / 255.0f);
    const __m128 scale_up = _mm_set1_ps(255.0f);
    const __m128 half = _mm_set1_ps(0.5f);
    const __m128 row_weight = _mm_set1_ps(row_alpha);
    const __m128 inverse_row = _mm_set1_ps(1.0f - row_alpha);
    int index;

    (void)user;
    for (index = 0; index < count; ++index) {
        __m128 column_weight = _mm_set1_ps(columns[index].alpha);
        __m128 inverse_column = _mm_set1_ps(1.0f - columns[index].alpha);
        __m128 top = _mm_add_ps(
            _mm_mul_ps(load_rgba(source_row0 + columns[index].x0 * 4),
                       inverse_column),
            _mm_mul_ps(load_rgba(source_row0 + columns[index].x1 * 4),
                       column_weight));
        __m128 bottom = _mm_add_ps(
            _mm_mul_ps(load_rgba(source_row1 + columns[index].x0 * 4),
                       inverse_column),
            _mm_mul_ps(load_rgba(source_row1 + columns[index].x1 * 4),
                       column_weight));
        __m128 value = _mm_add_ps(_mm_mul_ps(top, inverse_row),
                                  _mm_mul_ps(bottom, row_weight));
        __m128i integer;
        int rgba[4];

        value = _mm_mul_ps(_mm_mul_ps(value, scale_down), scale_up);
        integer = _mm_cvttps_epi32(_mm_add_ps(value, half));
        _mm_storeu_si128((__m128i *)rgba, integer);
        destination[index * 4 + 0] = (unsigned char)rgba[2];
        destination[index * 4 + 1] = (unsigned char)rgba[1];
        destination[index * 4 + 2] = (unsigned char)rgba[0];
        destination[index * 4 + 3] = 255;
    }
    return 1;
}
#elif defined(__ARM_NEON)
static float32x4_t load_rgba(const unsigned char *pixel)
{
    uint32_t packed;
    uint8x8_t bytes;
    uint16x8_t words;

    memcpy(&packed, pixel, sizeof(packed));
    bytes = vreinterpret_u8_u32(vdup_n_u32(packed));
    words = vmovl_u8(bytes);
    return vcvtq_f32_u32(vmovl_u16(vget_low_u16(words)));
}

static int linear_rgba8888_to_xrgb8888_neon(
    void *user, unsigned char *destination,
    const unsigned char *source_row0, const unsigned char *source_row1,
    const NTGLlinearColumn *columns, int count, float row_alpha)
{
    const float32x4_t scale_down = vdupq_n_f32(1.0f / 255.0f);
    const float32x4_t scale_up = vdupq_n_f32(255.0f);
    const float32x4_t half = vdupq_n_f32(0.5f);
    int index;

    (void)user;
    for (index = 0; index < count; ++index) {
        float column_alpha = columns[index].alpha;
        float32x4_t top = vmlaq_n_f32(
            vmulq_n_f32(load_rgba(source_row0 + columns[index].x0 * 4),
                        1.0f - column_alpha),
            load_rgba(source_row0 + columns[index].x1 * 4), column_alpha);
        float32x4_t bottom = vmlaq_n_f32(
            vmulq_n_f32(load_rgba(source_row1 + columns[index].x0 * 4),
                        1.0f - column_alpha),
            load_rgba(source_row1 + columns[index].x1 * 4), column_alpha);
        float32x4_t value = vmlaq_n_f32(
            vmulq_n_f32(top, 1.0f - row_alpha), bottom, row_alpha);
        uint32x4_t integer;
        uint32_t rgba[4];

        value = vmulq_f32(vmulq_f32(value, scale_down), scale_up);
        integer = vcvtq_u32_f32(vaddq_f32(value, half));
        vst1q_u32(rgba, integer);
        destination[index * 4 + 0] = (unsigned char)rgba[2];
        destination[index * 4 + 1] = (unsigned char)rgba[1];
        destination[index * 4 + 2] = (unsigned char)rgba[0];
        destination[index * 4 + 3] = 255;
    }
    return 1;
}
#endif

const char *mesaGLInitSIMDPixelOps(NTGLpixelOps *operations)
{
    if (!operations)
        return NULL;
    memset(operations, 0, sizeof(*operations));
#if defined(__x86_64__) || defined(__i386__)
#if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx2")) {
        operations->linear_rgba8888_to_xrgb8888 =
            linear_rgba8888_to_xrgb8888_avx2;
        return "avx2";
    }
#endif
    operations->linear_rgba8888_to_xrgb8888 =
        linear_rgba8888_to_xrgb8888_sse2;
    return "sse2";
#elif defined(__ARM_NEON)
    operations->linear_rgba8888_to_xrgb8888 =
        linear_rgba8888_to_xrgb8888_neon;
    return "neon";
#else
    return NULL;
#endif
}
