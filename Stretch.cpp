#include "Stretch.h"

static PF_Err
About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg, "%s v%d.%d\r%s", STR(StrID_Name), MAJOR_VERSION, MINOR_VERSION, STR(StrID_Description));
    return PF_Err_NONE;
}

static PF_Err
GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;
    out_data->out_flags2 |= PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
    return PF_Err_NONE;
}

static PF_Err
FrameSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
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

#include <mutex>
std::mutex renderMutex;
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

template <typename Pixel>
static PF_Err RenderGeneric(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;

    PF_EffectWorld* input = &params[0]->u.ld;
    Pixel* input_pixels = reinterpret_cast<Pixel*>(input->data);
    Pixel* output_pixels = reinterpret_cast<Pixel*>(output->data);

    int width = output->width;
    int height = output->height;
    int input_row_pixels = input->rowbytes / sizeof(Pixel);
    int output_row_pixels = output->rowbytes / sizeof(Pixel);

    int anchor_x = (params[ANCHOR_POINT_ID]->u.td.x_value >> 16);
    int anchor_y = (params[ANCHOR_POINT_ID]->u.td.y_value >> 16);

    float angle_param_value = (float)(params[ANGLE_ID]->u.ad.value >> 16);
    angle_param_value = fmod(angle_param_value, 360.0f);
    const float PI = 3.14159265358979323846f;
    float angle = angle_param_value * (PI / 180.0f);

    int shift_amount = static_cast<int>(params[SHIFT_AMOUNT_ID]->u.fs_d.value);
    float downsize_x = static_cast<float>(in_data->downsample_x.den) / static_cast<float>(in_data->downsample_x.num);
    float downsize_y = static_cast<float>(in_data->downsample_y.den) / static_cast<float>(in_data->downsample_y.num);

    int shift_pixels = static_cast<int>(shift_amount / min(downsize_x, downsize_y));

    int direction = params[DIRECTION_ID]->u.pd.value;

    if (shift_amount == 0) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                output_pixels[y * output_row_pixels + x] = input_pixels[y * input_row_pixels + x];
            }
        }
        return err;
    }

    int actual_shift = shift_pixels;
    if (direction == 1) { // Both
        actual_shift = shift_pixels / 2;
    }

    float perpendicular_x = -sin(angle);
    float perpendicular_y = cos(angle);

    float parallel_x = cos(angle);
    float parallel_y = sin(angle);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Pixel* output_pixel = output_pixels + y * output_row_pixels + x;

            float rel_x = (x - anchor_x);
            float rel_y = (y - anchor_y);

            float signed_distance_pixels = rel_x * perpendicular_x + rel_y * perpendicular_y;

            float parallel_distance = rel_x * parallel_x + rel_y * parallel_y;

            int border_x = static_cast<int>(anchor_x + (parallel_distance * parallel_x));
            int border_y = static_cast<int>(anchor_y + (parallel_distance * parallel_y));

            border_x = std::clamp(border_x, 0, width - 1);
            border_y = std::clamp(border_y, 0, height - 1);

            Pixel* border_pixel = input_pixels + border_y * input_row_pixels + border_x;

            int src_x = x;
            int src_y = y;
            bool use_border_pixel = false;

            switch (direction) {
            case 1: // Both directions
                if (fabs(signed_distance_pixels) < actual_shift) {
                    use_border_pixel = true;
                }
                else if (signed_distance_pixels > 0) {
                    src_x = static_cast<int>(x - perpendicular_x * actual_shift);
                    src_y = static_cast<int>(y - perpendicular_y * actual_shift);
                }
                else {
                    src_x = static_cast<int>(x + perpendicular_x * actual_shift);
                    src_y = static_cast<int>(y + perpendicular_y * actual_shift);
                }
                break;

            case 2: // Forward direction
                if (signed_distance_pixels >= 0 && signed_distance_pixels < actual_shift) {
                    use_border_pixel = true;
                }
                else if (signed_distance_pixels >= actual_shift) {
                    src_x = static_cast<int>(x - perpendicular_x * actual_shift);
                    src_y = static_cast<int>(y - perpendicular_y * actual_shift);
                }
                break;

            case 3: // Backward direction
                if (signed_distance_pixels <= 0 && signed_distance_pixels > -actual_shift) {
                    use_border_pixel = true;
                }
                else if (signed_distance_pixels <= -actual_shift) {
                    src_x = static_cast<int>(x + perpendicular_x * actual_shift);
                    src_y = static_cast<int>(y + perpendicular_y * actual_shift);
                }
                break;
            }

            // 出力ピクセルを設定
            if (use_border_pixel) {
                *output_pixel = *border_pixel;
            }
            else {
                // 範囲チェック
                src_x = std::clamp(src_x, 0, width - 1);
                src_y = std::clamp(src_y, 0, height - 1);

                Pixel* input_pixel = input_pixels + src_y * input_row_pixels + src_x;
                *output_pixel = *input_pixel;
            }
        }
    }

    return err;
}

static PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;

    int pitch = output->rowbytes / output->width;

    if (output->world_flags & PF_WorldFlag_DEEP) {
        if (pitch < 6) {
            err = RenderGeneric<PF_Pixel16>(in_data, out_data, params, output);
        }
        else if (pitch < 12) {
            err = RenderGeneric<PF_Pixel16>(in_data, out_data, params, output);
        }
        else {
            err = RenderGeneric<PF_PixelFloat>(in_data, out_data, params, output);
        }
    }
    else {
        err = RenderGeneric<PF_Pixel>(in_data, out_data, params, output);
    }

    return err;
}

extern "C" DllExport
PF_Err
PluginDataEntryFunction2(PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr, SPBasicSuite* inSPBasicSuitePtr, const char* inHostName, const char* inHostVersion)
{
    PF_Err result = PF_Err_INVALID_CALLBACK;

    result = PF_REGISTER_EFFECT_EXT2(
        inPtr,
        inPluginDataCallBackPtr,
        "Stretch_v2",              // Name
        "Stretch_v2",             // Match Name
        "Stretch",                 // Category
        AE_RESERVED_INFO,          // Reserved Info
        "EffectMain",              // Entry point
        "https://www.adobe.com");  // support URL

    return result;
}

PF_Err
EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void* extra)
{
    PF_Err err = PF_Err_NONE;
    try
    {
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
        case PF_Cmd_RENDER:
            err = Render(in_data, out_data, params, output);
            break;
        }
    }
    catch (PF_Err& thrown_err)
    {
        err = thrown_err;
    }
    return err;
}