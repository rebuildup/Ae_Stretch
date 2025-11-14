#include "Stretch.h"

#include <algorithm>
#include <cmath>

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
        PF_COPY(*input, output, NULL, NULL);
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
        PF_COPY(*input, output, NULL, NULL);
        return err;
    }

    const float shift_x = perpendicular_x * static_cast<float>(actual_shift);
    const float shift_y = perpendicular_y * static_cast<float>(actual_shift);

    // For directional modes where half of the image is unchanged,
    // pre-fill output with input so early-outs are correct.
    if (direction == 2 || direction == 3)
    {
        PF_COPY(*input, output, NULL, NULL);
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

            // Fast early-out for half-plane that remains unchanged.
            if (!((direction == 2 && sd < 0.0f) || (direction == 3 && sd > 0.0f)))
            {
                int border_x = static_cast<int>(border_x_f);
                int border_y = static_cast<int>(border_y_f);

                if (border_x < 0)
                {
                    border_x = 0;
                }
                else if (border_x >= width)
                {
                    border_x = width - 1;
                }

                if (border_y < 0)
                {
                    border_y = 0;
                }
                else if (border_y >= height)
                {
                    border_y = height - 1;
                }

                Pixel *border_pixel = input_pixels + border_y * input_row_pixels + border_x;

                int src_x = x;
                int src_y = y;
                bool use_border_pixel = false;

                switch (direction)
                {
                case 1: // Both directions
                    if (std::fabs(sd) < static_cast<float>(actual_shift))
                    {
                        use_border_pixel = true;
                    }
                    else if (sd > 0.0f)
                    {
                        src_x = static_cast<int>(static_cast<float>(x) - shift_x);
                        src_y = static_cast<int>(static_cast<float>(y) - shift_y);
                    }
                    else
                    {
                        src_x = static_cast<int>(static_cast<float>(x) + shift_x);
                        src_y = static_cast<int>(static_cast<float>(y) + shift_y);
                    }
                    break;

                case 2: // Forward direction
                    if (sd >= 0.0f && sd < static_cast<float>(actual_shift))
                    {
                        use_border_pixel = true;
                    }
                    else if (sd >= static_cast<float>(actual_shift))
                    {
                        src_x = static_cast<int>(static_cast<float>(x) - shift_x);
                        src_y = static_cast<int>(static_cast<float>(y) - shift_y);
                    }
                    break;

                case 3: // Backward direction
                    if (sd <= 0.0f && sd > -static_cast<float>(actual_shift))
                    {
                        use_border_pixel = true;
                    }
                    else if (sd <= -static_cast<float>(actual_shift))
                    {
                        src_x = static_cast<int>(static_cast<float>(x) + shift_x);
                        src_y = static_cast<int>(static_cast<float>(y) + shift_y);
                    }
                    break;

                default:
                    break;
                }

                if (use_border_pixel)
                {
                    *output_pixel = *border_pixel;
                }
                else
                {
                    if (src_x < 0)
                    {
                        src_x = 0;
                    }
                    else if (src_x >= width)
                    {
                        src_x = width - 1;
                    }

                    if (src_y < 0)
                    {
                        src_y = 0;
                    }
                    else if (src_y >= height)
                    {
                        src_y = height - 1;
                    }

                    Pixel *input_pixel = input_pixels + src_y * input_row_pixels + src_x;
                    *output_pixel = *input_pixel;
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
