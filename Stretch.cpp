#include "Stretch.h"

#include <algorithm>
#include <cmath>
#include <limits>

// -----------------------------------------------------------------------------
// UI / boilerplate
// -----------------------------------------------------------------------------

static PF_Err
About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    if (in_data && in_data->pica_basicP)
    {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg,
                                              "%s v%d.%d\r%s",
                                              STR(StrID_Name),
                                              MAJOR_VERSION,
                                              MINOR_VERSION,
                                              STR(StrID_Description));
    }
    else
    {
        // Fallback if pica_basicP is NULL
        PF_SPRINTF(out_data->return_msg,
                   "%s v%d.%d\r%s",
                   STR(StrID_Name),
                   MAJOR_VERSION,
                   MINOR_VERSION,
                   STR(StrID_Description));
    }

    return PF_Err_NONE;
}

static PF_Err
GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;
    out_data->out_flags2 |= PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
    return PF_Err_NONE;
}

static PF_Err
FrameSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);

    PF_ADD_POINT("Anchor Point",
                 50, 50,
                 false,
                 ANCHOR_POINT_ID);

    PF_ADD_ANGLE("Angle", 0, ANGLE_ID);

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
        SHIFT_AMOUNT_ID);

    PF_ADD_POPUP(
        "Direction",
        3,
        1,
        "Both|Forward|Backward",
        DIRECTION_ID);

    out_data->num_params = STRETCH_NUM_PARAMS;
    return PF_Err_NONE;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

template <typename PixelT>
struct PixelTraits;

template <>
struct PixelTraits<PF_Pixel>
{
    using ChannelType = A_u_char;
    static inline float ToFloat(ChannelType v) { return static_cast<float>(v); }
    static inline ChannelType FromFloat(float v)
    {
        v = std::clamp(v, 0.0f, 255.0f);
        return static_cast<ChannelType>(v + 0.5f);
    }
};

template <>
struct PixelTraits<PF_Pixel16>
{
    using ChannelType = A_u_short;
    static inline float ToFloat(ChannelType v) { return static_cast<float>(v); }
    static inline ChannelType FromFloat(float v)
    {
        constexpr float max_val = static_cast<float>(std::numeric_limits<ChannelType>::max());
        v = std::clamp(v, 0.0f, max_val);
        return static_cast<ChannelType>(v + 0.5f);
    }
};

template <>
struct PixelTraits<PF_PixelFloat>
{
    using ChannelType = PF_FpShort;
    static inline float ToFloat(ChannelType v) { return static_cast<float>(v); }
    static inline ChannelType FromFloat(float v) { return static_cast<ChannelType>(v); }
};

template <typename Pixel>
static inline Pixel SampleBilinearClamped(const Pixel *pixels,
                                          int row_pixels,
                                          float xf,
                                          float yf,
                                          int width,
                                          int height)
{
    if (width <= 0 || height <= 0)
    {
        return Pixel{};
    }

    const float max_x = static_cast<float>(width - 1);
    const float max_y = static_cast<float>(height - 1);
    xf = std::clamp(xf, 0.0f, max_x);
    yf = std::clamp(yf, 0.0f, max_y);

    const int x0 = static_cast<int>(xf);
    const int y0 = static_cast<int>(yf);
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);

    const float tx = xf - static_cast<float>(x0);
    const float ty = yf - static_cast<float>(y0);

    const Pixel *row0 = pixels + y0 * row_pixels;
    const Pixel *row1 = pixels + y1 * row_pixels;

    const Pixel &p00 = row0[x0];
    const Pixel &p10 = row0[x1];
    const Pixel &p01 = row1[x0];
    const Pixel &p11 = row1[x1];

    auto blend_channel = [&](auto Pixel::*member) -> typename PixelTraits<Pixel>::ChannelType {
        const float c00 = PixelTraits<Pixel>::ToFloat(p00.*member);
        const float c10 = PixelTraits<Pixel>::ToFloat(p10.*member);
        const float c01 = PixelTraits<Pixel>::ToFloat(p01.*member);
        const float c11 = PixelTraits<Pixel>::ToFloat(p11.*member);

        const float c0 = c00 + (c10 - c00) * tx;
        const float c1 = c01 + (c11 - c01) * tx;
        return PixelTraits<Pixel>::FromFloat(c0 + (c1 - c0) * ty);
    };

    Pixel result{};
    result.alpha = blend_channel(&Pixel::alpha);
    result.red = blend_channel(&Pixel::red);
    result.green = blend_channel(&Pixel::green);
    result.blue = blend_channel(&Pixel::blue);

    return result;
}

// -----------------------------------------------------------------------------
// Core stretch implementation (generic over pixel type)
// -----------------------------------------------------------------------------

template <typename Pixel>
static PF_Err RenderGeneric(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    PF_Err err = PF_Err_NONE;

    PF_EffectWorld *input = &params[0]->u.ld;
    Pixel *input_pixels = reinterpret_cast<Pixel *>(input->data);
    Pixel *output_pixels = reinterpret_cast<Pixel *>(output->data);

    const int width = output->width;
    const int height = output->height;
    const int input_row_pixels = input->rowbytes / static_cast<int>(sizeof(Pixel));
    const int output_row_pixels = output->rowbytes / static_cast<int>(sizeof(Pixel));

    const int anchor_x = (params[ANCHOR_POINT_ID]->u.td.x_value >> 16);
    const int anchor_y = (params[ANCHOR_POINT_ID]->u.td.y_value >> 16);

    float angle_param_value = static_cast<float>(params[ANGLE_ID]->u.ad.value >> 16);
    angle_param_value = std::fmod(angle_param_value, 360.0f);
    const float angle_rad = angle_param_value * (static_cast<float>(M_PI) / 180.0f);

    const int shift_amount = static_cast<int>(params[SHIFT_AMOUNT_ID]->u.fs_d.value);
    const float downsize_x = static_cast<float>(in_data->downsample_x.den) / static_cast<float>(in_data->downsample_x.num);
    const float downsize_y = static_cast<float>(in_data->downsample_y.den) / static_cast<float>(in_data->downsample_y.num);

    const float downsample = std::min(downsize_x, downsize_y);
    const int shift_pixels = (downsample > 0.0f) ? static_cast<int>(shift_amount / downsample) : shift_amount;

    const int direction = params[DIRECTION_ID]->u.pd.value;

    if (shift_amount == 0 || shift_pixels <= 0)
    {
        // No-op stretch: just copy input to output.
        PF_COPY(input, output, NULL, NULL);
        return err;
    }

    const float perpendicular_x = -std::sin(angle_rad);
    const float perpendicular_y = std::cos(angle_rad);

    const float parallel_x = std::cos(angle_rad);
    const float parallel_y = std::sin(angle_rad);

    int actual_shift = shift_pixels;
    if (direction == 1)
    { // Both
        actual_shift = shift_pixels / 2;
    }

    if (actual_shift <= 0)
    {
        // Effective shift vanished after direction adjustment.
        PF_COPY(input, output, NULL, NULL);
        return err;
    }

    const float shift_x = perpendicular_x * static_cast<float>(actual_shift);
    const float shift_y = perpendicular_y * static_cast<float>(actual_shift);
    const float actual_shift_f = static_cast<float>(actual_shift);

    // For directional modes where half of the image is unchanged,
    // pre-fill output with input so early-outs are correct.
    if (direction == 2 || direction == 3)
    {
        PF_COPY(input, output, NULL, NULL);
    }

    // Single-threaded scanline processing with per-row precomputation.
    for (int y = 0; y < height; ++y)
    {
        Pixel *output_row = output_pixels + y * output_row_pixels;

        const float rel_y = static_cast<float>(y - anchor_y);
        const float base_rel_x = static_cast<float>(-anchor_x);

        float signed_distance = base_rel_x * perpendicular_x + rel_y * perpendicular_y;

        const float par_base = base_rel_x * parallel_x + rel_y * parallel_y;
        float border_x_f = static_cast<float>(anchor_x) + par_base * parallel_x;
        float border_y_f = static_cast<float>(anchor_y) + par_base * parallel_y;

        const float signed_step = perpendicular_x;
        const float border_x_step = parallel_x * parallel_x;
        const float border_y_step = parallel_x * parallel_y;

        for (int x = 0; x < width; ++x)
        {
            Pixel *output_pixel = output_row + x;

            const float sd = signed_distance;

            const bool skip_forward = (direction == 2 && sd < 0.0f);
            const bool skip_backward = (direction == 3 && sd > 0.0f);

            if (!(skip_forward || skip_backward))
            {
                const float x_f = static_cast<float>(x);
                const float y_f = static_cast<float>(y);

                bool use_border_pixel = false;
                float sample_x = x_f;
                float sample_y = y_f;

                switch (direction)
                {
                case 1: // Both directions
                default:
                    if (std::fabs(sd) < actual_shift_f)
                    {
                        use_border_pixel = true;
                    }
                    else if (sd > 0.0f)
                    {
                        sample_x = x_f - shift_x;
                        sample_y = y_f - shift_y;
                    }
                    else
                    {
                        sample_x = x_f + shift_x;
                        sample_y = y_f + shift_y;
                    }
                    break;

                case 2: // Forward direction
                    if (sd < actual_shift_f)
                    {
                        use_border_pixel = true;
                    }
                    else
                    {
                        sample_x = x_f - shift_x;
                        sample_y = y_f - shift_y;
                    }
                    break;

                case 3: // Backward direction
                    if (-sd < actual_shift_f)
                    {
                        use_border_pixel = true;
                    }
                    else
                    {
                        sample_x = x_f + shift_x;
                        sample_y = y_f + shift_y;
                    }
                    break;
                }

                if (use_border_pixel)
                {
                    *output_pixel = SampleBilinearClamped(input_pixels,
                                                          input_row_pixels,
                                                          border_x_f,
                                                          border_y_f,
                                                          width,
                                                          height);
                }
                else
                {
                    *output_pixel = SampleBilinearClamped(input_pixels,
                                                          input_row_pixels,
                                                          sample_x,
                                                          sample_y,
                                                          width,
                                                          height);
                }
            }

            signed_distance += signed_step;
            border_x_f += border_x_step;
            border_y_f += border_y_step;
        }
    }

    return err;
}

// -----------------------------------------------------------------------------
// Bit-depth aware dispatch
// -----------------------------------------------------------------------------

static PF_Err Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    PF_Err err = PF_Err_NONE;

    // Prefer explicit deep-color handling but keep logic simple and robust.
    if (output->world_flags & PF_WorldFlag_DEEP)
    {
        const A_long bytes_per_pixel = output->rowbytes / output->width;

        if (bytes_per_pixel == static_cast<A_long>(sizeof(PF_Pixel16)))
        {
            // 16-bit per channel
            err = RenderGeneric<PF_Pixel16>(in_data, out_data, params, output);
        }
        else if (bytes_per_pixel == static_cast<A_long>(sizeof(PF_PixelFloat)))
        {
            // 32-bit float per channel
            err = RenderGeneric<PF_PixelFloat>(in_data, out_data, params, output);
        }
        else
        {
            // Fallback for unexpected formats: treat as 16-bit.
            err = RenderGeneric<PF_Pixel16>(in_data, out_data, params, output);
        }
    }
    else
    {
        // Standard 8-bit
        err = RenderGeneric<PF_Pixel>(in_data, out_data, params, output);
    }

    return err;
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

extern "C" DllExport
PF_Err
PluginDataEntryFunction2(PF_PluginDataPtr inPtr,
                         PF_PluginDataCB2 inPluginDataCallBackPtr,
                         SPBasicSuite *inSPBasicSuitePtr,
                         const char *inHostName,
                         const char *inHostVersion)
{
    PF_Err result = PF_Err_INVALID_CALLBACK;

    result = PF_REGISTER_EFFECT_EXT2(
        inPtr,
        inPluginDataCallBackPtr,
        "Stretch_v2",                 // Name
        "361do Stretch_v2",           // Match Name
        "361do_plugins",              // Category
        AE_RESERVED_INFO,             // Reserved Info
        "EffectMain",                 // Entry point
        "https://x.com/361do_sleep"); // support URL

    return result;
}

extern "C" DllExport
PF_Err
EffectMain(PF_Cmd cmd,
           PF_InData *in_data,
           PF_OutData *out_data,
           PF_ParamDef *params[],
           PF_LayerDef *output,
           void *extra)
{
    PF_Err err = PF_Err_NONE;

    switch (cmd)
    {
    case PF_Cmd_ABOUT:
        err = About(in_data, out_data, params, output);
        break;

    case PF_Cmd_GLOBAL_SETUP:
        err = GlobalSetup(in_data, out_data, params, output);
        break;

    case PF_Cmd_PARAMS_SETUP:
        err = ParamsSetup(in_data, out_data, params, output);
        break;

    case PF_Cmd_FRAME_SETUP:
        err = FrameSetup(in_data, out_data, params, output);
        break;

    case PF_Cmd_RENDER:
        err = Render(in_data, out_data, params, output);
        break;

    default:
        break;
    }

    return err;
}
