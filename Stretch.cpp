#include "Stretch.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <thread>
#include <cstring>

// -----------------------------------------------------------------------------
// UI / boilerplate
// -----------------------------------------------------------------------------

static PF_Err
About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    (void)params;
    (void)output;

    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg,
                                          "%s v%d.%d\r%s",
                                          STR(StrID_Name),
                                          MAJOR_VERSION,
                                          MINOR_VERSION,
                                          STR(StrID_Description));
    return PF_Err_NONE;
}

static PF_Err
GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    (void)in_data;
    (void)params;
    (void)output;

    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    // Support 16-bit (Deep Color), Multi-Frame Rendering
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_PIX_INDEPENDENT;
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
    return PF_Err_NONE;
}

static PF_Err
FrameSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    (void)in_data;
    (void)out_data;
    (void)params;
    (void)output;
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    (void)in_data;
    (void)params;
    (void)output;

    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);

    PF_ADD_FLOAT_SLIDERX(
        "Shift Amount",
        0,
        10000,
        0,
        500,
        0,
        PF_Precision_INTEGER,
        0,
        0,
        SHIFT_AMOUNT_DISK_ID);

    AEFX_CLR_STRUCT(def);

    PF_ADD_POINT("Anchor Point",
                 50, 50,
                 false,
                 ANCHOR_POINT_DISK_ID);

    AEFX_CLR_STRUCT(def);

    PF_ADD_ANGLE("Angle", 0, ANGLE_DISK_ID);

    AEFX_CLR_STRUCT(def);

    PF_ADD_POPUP(
        "Direction",
        3,
        1,
        "Both|Forward|Backward",
        DIRECTION_DISK_ID);

    out_data->num_params = STRETCH_NUM_PARAMS;
    return err;
}



#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

template <typename T>
static inline T ClampScalar(T value, T min_value, T max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

// -----------------------------------------------------------------------------
// Pixel Traits
// -----------------------------------------------------------------------------

template <typename PixelT>
struct PixelTraits;

template <>
struct PixelTraits<PF_Pixel>
{
    using ChannelType = A_u_char;
    static constexpr float MAX_VAL = 255.0f;
    static inline float ToFloat(ChannelType v) { return static_cast<float>(v); }
    static inline ChannelType FromFloat(float v)
    {
        return static_cast<ChannelType>(ClampScalar(v, 0.0f, MAX_VAL) + 0.5f);
    }
};

template <>
struct PixelTraits<PF_Pixel16>
{
    using ChannelType = A_u_short;
    static constexpr float MAX_VAL = 32768.0f;
    static inline float ToFloat(ChannelType v) { return static_cast<float>(v); }
    static inline ChannelType FromFloat(float v)
    {
        return static_cast<ChannelType>(ClampScalar(v, 0.0f, MAX_VAL) + 0.5f);
    }
};

template <>
struct PixelTraits<PF_PixelFloat>
{
    using ChannelType = PF_FpShort;
    static inline float ToFloat(ChannelType v) { return static_cast<float>(v); }
    static inline ChannelType FromFloat(float v) { return static_cast<ChannelType>(v); }
};

// -----------------------------------------------------------------------------
// Sampling
// -----------------------------------------------------------------------------

template <typename Pixel>
static inline Pixel SampleBilinear(const A_u_char *base_ptr,
                                   A_long rowbytes,
                                   float xf,
                                   float yf,
                                   int width,
                                   int height)
{
    // Clamp coordinates to valid range
    xf = ClampScalar(xf, 0.0f, static_cast<float>(width - 1));
    yf = ClampScalar(yf, 0.0f, static_cast<float>(height - 1));

    const int x0 = static_cast<int>(xf);
    const int y0 = static_cast<int>(yf);
    const int x1 = (std::min)(x0 + 1, width - 1);
    const int y1 = (std::min)(y0 + 1, height - 1);

    const float tx = xf - static_cast<float>(x0);
    const float ty = yf - static_cast<float>(y0);

    const Pixel *row0 = reinterpret_cast<const Pixel *>(base_ptr + static_cast<A_long>(y0) * rowbytes);
    const Pixel *row1 = reinterpret_cast<const Pixel *>(base_ptr + static_cast<A_long>(y1) * rowbytes);

    const Pixel &p00 = row0[x0];
    const Pixel &p10 = row0[x1];
    const Pixel &p01 = row1[x0];
    const Pixel &p11 = row1[x1];

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    Pixel result;
    result.alpha = PixelTraits<Pixel>::FromFloat(
        lerp(lerp(PixelTraits<Pixel>::ToFloat(p00.alpha), PixelTraits<Pixel>::ToFloat(p10.alpha), tx),
            lerp(PixelTraits<Pixel>::ToFloat(p01.alpha), PixelTraits<Pixel>::ToFloat(p11.alpha), tx), ty));
    result.red = PixelTraits<Pixel>::FromFloat(
        lerp(lerp(PixelTraits<Pixel>::ToFloat(p00.red), PixelTraits<Pixel>::ToFloat(p10.red), tx),
            lerp(PixelTraits<Pixel>::ToFloat(p01.red), PixelTraits<Pixel>::ToFloat(p11.red), tx), ty));
    result.green = PixelTraits<Pixel>::FromFloat(
        lerp(lerp(PixelTraits<Pixel>::ToFloat(p00.green), PixelTraits<Pixel>::ToFloat(p10.green), tx),
            lerp(PixelTraits<Pixel>::ToFloat(p01.green), PixelTraits<Pixel>::ToFloat(p11.green), tx), ty));
    result.blue = PixelTraits<Pixel>::FromFloat(
        lerp(lerp(PixelTraits<Pixel>::ToFloat(p00.blue), PixelTraits<Pixel>::ToFloat(p10.blue), tx),
            lerp(PixelTraits<Pixel>::ToFloat(p01.blue), PixelTraits<Pixel>::ToFloat(p11.blue), tx), ty));
    return result;
}

// -----------------------------------------------------------------------------
// Stretch rendering helpers
// -----------------------------------------------------------------------------

template <typename Pixel>
struct StretchRenderContext
{
    const A_u_char *input_base;
    A_u_char *output_base;
    A_long input_rowbytes;
    A_long output_rowbytes;
    int width;
    int height;
    int input_width;
    int input_height;

    // Geometry
    float anchor_x;
    float anchor_y;
    float effective_shift;
    float shift_vec_x;
    float shift_vec_y;
    float perp_x;
    float perp_y;
    float para_x;
    float para_y;
};

template <typename Pixel>
static inline void ProcessRowsBoth(const StretchRenderContext<Pixel> &ctx, int start_y, int end_y)
{
    const float eff = ctx.effective_shift;
    const float shift_vec_x = ctx.shift_vec_x;
    const float shift_vec_y = ctx.shift_vec_y;
    const float perp_x = ctx.perp_x;
    const float perp_y = ctx.perp_y;
    const float para_x = ctx.para_x;
    const float para_y = ctx.para_y;
    const float anchor_x_f = ctx.anchor_x;
    const float anchor_y_f = ctx.anchor_y;

    for (int y = start_y; y < end_y; ++y) {
        const float yf = static_cast<float>(y);
        const float dy = yf - anchor_y_f;

        const float dx0 = -anchor_x_f;
        const float dxN = static_cast<float>(ctx.width - 1) - anchor_x_f;

        const float base_perp = dy * perp_y;
        const float dist0 = dx0 * perp_x + base_perp;
        const float distN = dxN * perp_x + base_perp;

        const float row_min = (std::min)(dist0, distN);
        const float row_max = (std::max)(dist0, distN);

        const float base_para = dy * para_y;
        float proj_len = dx0 * para_x + base_para;

        float sample_x = 0.0f;
        const float sample_y = yf;

        Pixel *out_row = reinterpret_cast<Pixel *>(ctx.output_base + static_cast<A_long>(y) * ctx.output_rowbytes);

        // Entire row is on the negative side beyond the gap -> all pixels shift in +direction
        if (row_max <= -eff) {
            for (int x = 0; x < ctx.width; ++x) {
                const float sx = sample_x + shift_vec_x;
                const float sy = sample_y + shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
                sample_x += 1.0f;
            }
            continue;
        }

        // Entire row is on the positive side beyond the gap -> all pixels shift in -direction
        if (row_min >= eff) {
            for (int x = 0; x < ctx.width; ++x) {
                const float sx = sample_x - shift_vec_x;
                const float sy = sample_y - shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
                sample_x += 1.0f;
            }
            continue;
        }

        // Entire row is inside the gap -> border sampling only
        if (row_min > -eff && row_max < eff) {
            for (int x = 0; x < ctx.width; ++x) {
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
                proj_len += para_x;
            }
            continue;
        }

        // General case: mix of negative side, gap, and positive side
        float dist = dist0;
        proj_len = dx0 * para_x + base_para;

        for (int x = 0; x < ctx.width; ++x) {
            float sx = sample_x;
            float sy = sample_y;

            if (dist > eff) {
                sx -= shift_vec_x;
                sy -= shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
            } else if (dist < -eff) {
                sx += shift_vec_x;
                sy += shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
            } else {
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
            }

            sample_x += 1.0f;
            dist += perp_x;
            proj_len += para_x;
        }
    }
}

template <typename Pixel>
static inline void ProcessRowsForward(const StretchRenderContext<Pixel> &ctx, int start_y, int end_y)
{
    const float eff = ctx.effective_shift;
    const float shift_vec_x = ctx.shift_vec_x;
    const float shift_vec_y = ctx.shift_vec_y;
    const float perp_x = ctx.perp_x;
    const float perp_y = ctx.perp_y;
    const float para_x = ctx.para_x;
    const float para_y = ctx.para_y;
    const float anchor_x_f = ctx.anchor_x;
    const float anchor_y_f = ctx.anchor_y;

    for (int y = start_y; y < end_y; ++y) {
        const float yf = static_cast<float>(y);
        const float dy = yf - anchor_y_f;

        const float dx0 = -anchor_x_f;
        const float dxN = static_cast<float>(ctx.width - 1) - anchor_x_f;

        const float base_perp = dy * perp_y;
        const float dist0 = dx0 * perp_x + base_perp;
        const float distN = dxN * perp_x + base_perp;

        const float row_min = (std::min)(dist0, distN);
        const float row_max = (std::max)(dist0, distN);

        Pixel *out_row = reinterpret_cast<Pixel *>(ctx.output_base + static_cast<A_long>(y) * ctx.output_rowbytes);
        const Pixel *in_row = reinterpret_cast<const Pixel *>(ctx.input_base + static_cast<A_long>(y) * ctx.input_rowbytes);

        const float base_para = dy * para_y;
        float proj_len = dx0 * para_x + base_para;

        float sample_x = 0.0f;
        const float sample_y = yf;

        // Entire row is behind the line (dist < 0) -> unchanged
        if (row_max < 0.0f) {
            std::memcpy(out_row, in_row, sizeof(Pixel) * ctx.width);
            continue;
        }

        // Entire row is fully shifted (dist >= eff)
        if (row_min >= eff) {
            for (int x = 0; x < ctx.width; ++x) {
                const float sx = sample_x - shift_vec_x;
                const float sy = sample_y - shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
                sample_x += 1.0f;
            }
            continue;
        }

        // Entire row is within gap: 0 <= dist < eff -> border only
        if (row_min >= 0.0f && row_max < eff) {
            for (int x = 0; x < ctx.width; ++x) {
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
                proj_len += para_x;
            }
            continue;
        }

        // General case
        float dist = dist0;
        proj_len = dx0 * para_x + base_para;

        for (int x = 0; x < ctx.width; ++x) {
            if (dist < 0.0f) {
                // Unchanged
                out_row[x] = in_row[x];
            } else if (dist < eff) {
                // Border
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
            } else {
                // Shifted
                const float sx = sample_x - shift_vec_x;
                const float sy = sample_y - shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
            }

            sample_x += 1.0f;
            dist += perp_x;
            proj_len += para_x;
        }
    }
}

template <typename Pixel>
static inline void ProcessRowsBackward(const StretchRenderContext<Pixel> &ctx, int start_y, int end_y)
{
    const float eff = ctx.effective_shift;
    const float shift_vec_x = ctx.shift_vec_x;
    const float shift_vec_y = ctx.shift_vec_y;
    const float perp_x = ctx.perp_x;
    const float perp_y = ctx.perp_y;
    const float para_x = ctx.para_x;
    const float para_y = ctx.para_y;
    const float anchor_x_f = ctx.anchor_x;
    const float anchor_y_f = ctx.anchor_y;

    for (int y = start_y; y < end_y; ++y) {
        const float yf = static_cast<float>(y);
        const float dy = yf - anchor_y_f;

        const float dx0 = -anchor_x_f;
        const float dxN = static_cast<float>(ctx.width - 1) - anchor_x_f;

        const float base_perp = dy * perp_y;
        const float dist0 = dx0 * perp_x + base_perp;
        const float distN = dxN * perp_x + base_perp;

        const float row_min = (std::min)(dist0, distN);
        const float row_max = (std::max)(dist0, distN);

        Pixel *out_row = reinterpret_cast<Pixel *>(ctx.output_base + static_cast<A_long>(y) * ctx.output_rowbytes);
        const Pixel *in_row = reinterpret_cast<const Pixel *>(ctx.input_base + static_cast<A_long>(y) * ctx.input_rowbytes);

        const float base_para = dy * para_y;
        float proj_len = dx0 * para_x + base_para;

        float sample_x = 0.0f;
        const float sample_y = yf;

        // Entire row is in front of the line (dist > 0) -> unchanged
        if (row_min > 0.0f) {
            std::memcpy(out_row, in_row, sizeof(Pixel) * ctx.width);
            continue;
        }

        // Entire row is fully shifted (dist <= -eff)
        if (row_max <= -eff) {
            for (int x = 0; x < ctx.width; ++x) {
                const float sx = sample_x + shift_vec_x;
                const float sy = sample_y + shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
                sample_x += 1.0f;
            }
            continue;
        }

        // Entire row is within gap: -eff < dist <= 0 -> border only
        if (row_min > -eff && row_max <= 0.0f) {
            for (int x = 0; x < ctx.width; ++x) {
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
                proj_len += para_x;
            }
            continue;
        }

        // General case
        float dist = dist0;
        proj_len = dx0 * para_x + base_para;

        for (int x = 0; x < ctx.width; ++x) {
            if (dist > 0.0f) {
                // Unchanged
                out_row[x] = in_row[x];
            } else if (dist > -eff) {
                // Border
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
            } else {
                // Shifted
                const float sx = sample_x + shift_vec_x;
                const float sy = sample_y + shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
            }

            sample_x += 1.0f;
            dist += perp_x;
            proj_len += para_x;
        }
    }
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

template <typename Pixel>
static PF_Err RenderGeneric(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    (void)out_data;

    PF_EffectWorld *input = &params[STRETCH_INPUT]->u.ld;

    const int width = output->width;
    const int height = output->height;
    const int input_width = input->width;
    const int input_height = input->height;

    if (width <= 0 || height <= 0) {
        return PF_Err_NONE;
    }

    const A_u_char *input_base = reinterpret_cast<const A_u_char *>(input->data);
    A_u_char *output_base = reinterpret_cast<A_u_char *>(output->data);
    const A_long input_rowbytes = input->rowbytes;
    const A_long output_rowbytes = output->rowbytes;

    // Parameters
    const int anchor_x = (params[STRETCH_ANCHOR_POINT]->u.td.x_value >> 16);
    const int anchor_y = (params[STRETCH_ANCHOR_POINT]->u.td.y_value >> 16);
    float angle_deg = static_cast<float>(params[STRETCH_ANGLE]->u.ad.value >> 16);
    const float angle_rad = angle_deg * (static_cast<float>(M_PI) / 180.0f);
    const float shift_amount = static_cast<float>(params[STRETCH_SHIFT_AMOUNT]->u.fs_d.value);
    const int direction = params[STRETCH_DIRECTION]->u.pd.value;

    // Downsample adjustment
    const float downsample_x = static_cast<float>(in_data->downsample_x.den) / static_cast<float>(in_data->downsample_x.num);
    const float downsample_y = static_cast<float>(in_data->downsample_y.den) / static_cast<float>(in_data->downsample_y.num);
    const float downsample = std::min(downsample_x, downsample_y);

    // Effective shift in pixels
    float effective_shift = (downsample > 0.0f) ? (shift_amount / downsample) : shift_amount;

    if (std::abs(effective_shift) < 0.01f) {
        return PF_COPY(input, output, nullptr, nullptr);
    }

    // Direction adjustment
    if (direction == 1) { // Both
        effective_shift *= 0.5f;
    }

    // Precompute vectors
    const float sn = std::sin(angle_rad);
    const float cs = std::cos(angle_rad);

    const float perp_x = -sn;
    const float perp_y = cs;

    const float shift_vec_x = perp_x * effective_shift;
    const float shift_vec_y = perp_y * effective_shift;

    // Parallel vector (along the "cut" line)
    const float para_x = cs;
    const float para_y = sn;

    StretchRenderContext<Pixel> ctx{};
    ctx.input_base = input_base;
    ctx.output_base = output_base;
    ctx.input_rowbytes = input_rowbytes;
    ctx.output_rowbytes = output_rowbytes;
    ctx.width = width;
    ctx.height = height;
    ctx.input_width = input_width;
    ctx.input_height = input_height;
    ctx.anchor_x = static_cast<float>(anchor_x);
    ctx.anchor_y = static_cast<float>(anchor_y);
    ctx.effective_shift = effective_shift;
    ctx.shift_vec_x = shift_vec_x;
    ctx.shift_vec_y = shift_vec_y;
    ctx.perp_x = perp_x;
    ctx.perp_y = perp_y;
    ctx.para_x = para_x;
    ctx.para_y = para_y;

    const int max_threads = std::max(1u, std::thread::hardware_concurrency());
    const int height_clamped = std::max(height, 1);
    const int num_threads = std::min(max_threads, height_clamped);
    const int rows_per_thread = (height_clamped + num_threads - 1) / num_threads;

    auto worker = [&](int start_y, int end_y) {
        if (direction == 1) {
            ProcessRowsBoth(ctx, start_y, end_y);
        } else if (direction == 2) {
            ProcessRowsForward(ctx, start_y, end_y);
        } else {
            ProcessRowsBackward(ctx, start_y, end_y);
        }
    };

    if (num_threads <= 1 || height <= 1) {
        worker(0, height);
    } else {
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        int start_y = 0;
        for (int t = 0; t < num_threads; ++t) {
            const int end_y = std::min(start_y + rows_per_thread, height);
            if (start_y >= end_y) {
                break;
            }
            threads.emplace_back(worker, start_y, end_y);
            start_y = end_y;
        }
        for (auto &th : threads) {
            th.join();
        }
    }

    return PF_Err_NONE;
}

static PF_Err Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    (void)out_data;

    if (output->world_flags & PF_WorldFlag_DEEP) {
        if (PF_WORLD_IS_DEEP(output)) {
            // 16-bit
            return RenderGeneric<PF_Pixel16>(in_data, out_data, params, output);
        }
    }

    int bpp = (output->width > 0) ? (output->rowbytes / output->width) : 0;
    if (bpp == static_cast<int>(sizeof(PF_PixelFloat))) {
        return RenderGeneric<PF_PixelFloat>(in_data, out_data, params, output);
    } else if (bpp == static_cast<int>(sizeof(PF_Pixel16))) {
        return RenderGeneric<PF_Pixel16>(in_data, out_data, params, output);
    } else {
        return RenderGeneric<PF_Pixel>(in_data, out_data, params, output);
    }
}

extern "C" DllExport
PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr,
                                PF_PluginDataCB2 inPluginDataCallBackPtr,
                                SPBasicSuite *inSPBasicSuitePtr,
                                const char *inHostName,
                                const char *inHostVersion)
{
    (void)inHostName;
    (void)inHostVersion;

    PF_Err result = PF_Err_INVALID_CALLBACK;
    result = PF_REGISTER_EFFECT_EXT2(
        inPtr,
        inPluginDataCallBackPtr,
        "Stretch",       // Name
        "361do Stretch", // Match Name
        "361do_plugins", // Category
        AE_RESERVED_INFO,
        "EffectMain",
        "https://github.com/rebuildup/Ae_Stretch");
    return result;
}

extern "C" DllExport
PF_Err EffectMain(PF_Cmd cmd,
                  PF_InData *in_data,
                  PF_OutData *out_data,
                  PF_ParamDef *params[],
                  PF_LayerDef *output,
                  void *extra)
{
    (void)extra;

    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:
                err = About(in_data, out_data, params, output);
                break;
            case PF_Cmd_GLOBAL_SETUP:
                err = GlobalSetup(in_data, out_data, params, output);
                break;
            case PF_Cmd_FRAME_SETUP:
                err = FrameSetup(in_data, out_data, params, output);
                break;
            case PF_Cmd_PARAMS_SETUP:
                err = ParamsSetup(in_data, out_data, params, output);
                break;
            case PF_Cmd_RENDER:
                err = Render(in_data, out_data, params, output);
                break;
            default:
                break;
        }
    } catch (...) {
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    return err;
}

