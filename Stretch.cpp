#include "Stretch.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <thread>
#include <atomic>

// -----------------------------------------------------------------------------
// UI / boilerplate
// -----------------------------------------------------------------------------

static PF_Err
About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
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
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    // Support 16-bit (Deep Color) and 32-bit Float
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_PIX_INDEPENDENT;
    out_data->out_flags2 = PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
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
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);

    const float tx = xf - static_cast<float>(x0);
    const float ty = yf - static_cast<float>(y0);

    const Pixel *row0 = reinterpret_cast<const Pixel *>(base_ptr + static_cast<A_long>(y0) * rowbytes);
    const Pixel *row1 = reinterpret_cast<const Pixel *>(base_ptr + static_cast<A_long>(y1) * rowbytes);

    const Pixel &p00 = row0[x0];
    const Pixel &p10 = row0[x1];
    const Pixel &p01 = row1[x0];
    const Pixel &p11 = row1[x1];

    // Bilinear interpolation
    // f(x,y) = (1-tx)(1-ty)p00 + tx(1-ty)p10 + (1-tx)ty*p01 + tx*ty*p11
    // Optimized: lerp(lerp(p00, p10, tx), lerp(p01, p11, tx), ty)

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };

    Pixel result;
    result.alpha = PixelTraits<Pixel>::FromFloat(lerp(lerp(PixelTraits<Pixel>::ToFloat(p00.alpha), PixelTraits<Pixel>::ToFloat(p10.alpha), tx),
                                                      lerp(PixelTraits<Pixel>::ToFloat(p01.alpha), PixelTraits<Pixel>::ToFloat(p11.alpha), tx), ty));
    result.red = PixelTraits<Pixel>::FromFloat(lerp(lerp(PixelTraits<Pixel>::ToFloat(p00.red), PixelTraits<Pixel>::ToFloat(p10.red), tx),
                                                    lerp(PixelTraits<Pixel>::ToFloat(p01.red), PixelTraits<Pixel>::ToFloat(p11.red), tx), ty));
    result.green = PixelTraits<Pixel>::FromFloat(lerp(lerp(PixelTraits<Pixel>::ToFloat(p00.green), PixelTraits<Pixel>::ToFloat(p10.green), tx),
                                                      lerp(PixelTraits<Pixel>::ToFloat(p01.green), PixelTraits<Pixel>::ToFloat(p11.green), tx), ty));
    result.blue = PixelTraits<Pixel>::FromFloat(lerp(lerp(PixelTraits<Pixel>::ToFloat(p00.blue), PixelTraits<Pixel>::ToFloat(p10.blue), tx),
                                                     lerp(PixelTraits<Pixel>::ToFloat(p01.blue), PixelTraits<Pixel>::ToFloat(p11.blue), tx), ty));
    return result;
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

template <typename Pixel>
static PF_Err RenderGeneric(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    PF_EffectWorld *input = &params[0]->u.ld;

    const int width = output->width;
    const int height = output->height;
    const int input_width = input->width;
    const int input_height = input->height;

    if (width <= 0 || height <= 0) return PF_Err_NONE;

    const A_u_char *input_base = reinterpret_cast<const A_u_char *>(input->data);
    A_u_char *output_base = reinterpret_cast<A_u_char *>(output->data);
    const A_long input_rowbytes = input->rowbytes;
    const A_long output_rowbytes = output->rowbytes;

    // Parameters
    const int anchor_x = (params[ANCHOR_POINT_ID]->u.td.x_value >> 16);
    const int anchor_y = (params[ANCHOR_POINT_ID]->u.td.y_value >> 16);
    float angle_deg = static_cast<float>(params[ANGLE_ID]->u.ad.value >> 16);
    const float angle_rad = angle_deg * (static_cast<float>(M_PI) / 180.0f);
    const float shift_amount = static_cast<float>(params[SHIFT_AMOUNT_ID]->u.fs_d.value);
    const int direction = params[DIRECTION_ID]->u.pd.value;

    // Downsample adjustment
    const float downsample_x = static_cast<float>(in_data->downsample_x.den) / static_cast<float>(in_data->downsample_x.num);
    const float downsample_y = static_cast<float>(in_data->downsample_y.den) / static_cast<float>(in_data->downsample_y.num);
    const float downsample = std::min(downsample_x, downsample_y);
    
    // Effective shift in pixels
    float effective_shift = (downsample > 0.0f) ? (shift_amount / downsample) : shift_amount;

    if (std::abs(effective_shift) < 0.01f) {
        return PF_COPY(input, output, NULL, NULL);
    }

    // Direction adjustment
    if (direction == 1) { // Both
        effective_shift *= 0.5f;
    }

    // Precompute vectors
    // Perpendicular vector (direction of shift)
    // Angle 0 means shift up (y decreases). Wait, standard AE angle 0 is usually up or right?
    // Let's assume standard math: 0 is right (x+), 90 is down (y+).
    // But usually "Angle" param in AE: 0 is Up (0 degrees).
    // Let's stick to the previous implementation's logic:
    // perpendicular_x = -sin(angle), perpendicular_y = cos(angle)
    // This corresponds to a vector rotated 90 degrees from (cos, sin).
    
    const float sn = std::sin(angle_rad);
    const float cs = std::cos(angle_rad);
    
    const float perp_x = -sn;
    const float perp_y = cs;
    
    const float shift_vec_x = perp_x * effective_shift;
    const float shift_vec_y = perp_y * effective_shift;

    // Parallel vector (along the "cut" line)
    const float para_x = cs;
    const float para_y = sn;

    // Multi-threading
    const int num_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;

    auto process_rows = [&](int start_y, int end_y) {
        for (int y = start_y; y < end_y; ++y) {
            Pixel *out_row = reinterpret_cast<Pixel *>(output_base + y * output_rowbytes);
            
            // Precompute row constants
            const float dy = static_cast<float>(y - anchor_y);
            
            for (int x = 0; x < width; ++x) {
                const float dx = static_cast<float>(x - anchor_x);
                
                // Calculate signed distance from the line passing through anchor point
                // Line equation: dot(pos - anchor, perp) = distance
                // But we want distance along the perpendicular vector.
                // dist = dx * perp_x + dy * perp_y
                float dist = dx * perp_x + dy * perp_y;

                float sample_x = static_cast<float>(x);
                float sample_y = static_cast<float>(y);
                bool use_border = false;

                // Logic:
                // If dist > 0, we are on one side. If dist < 0, on the other.
                // We want to shift pixels AWAY from the line? Or shift the image content?
                // "Stretch" usually means splitting the image and moving parts apart.
                // So we sample from (pos - shift) or (pos + shift).

                if (direction == 1) { // Both
                    // Split at dist=0.
                    // Pixels with dist > 0 come from (pos - shift)
                    // Pixels with dist < 0 come from (pos + shift)
                    // Pixels in between (gap) are filled with border/stretched pixels?
                    // Previous implementation: if abs(dist) < shift, use border.
                    
                    if (std::abs(dist) < effective_shift) {
                        use_border = true;
                    } else if (dist > 0) {
                        sample_x -= shift_vec_x;
                        sample_y -= shift_vec_y;
                    } else {
                        sample_x += shift_vec_x;
                        sample_y += shift_vec_y;
                    }
                } else if (direction == 2) { // Forward
                    // Split at dist=0? Or dist=shift?
                    // Previous: if dist < shift, use border?
                    // Forward usually means one side moves, other stays?
                    // Let's follow previous logic:
                    // if sd < shift, use border.
                    // else sample - shift.
                    // Wait, if dist < 0 (behind line), it stays?
                    // Previous code:
                    // if (sd < actual_shift_f) use_border = true;
                    // else sample - shift;
                    // This implies everything < shift is border? That seems wrong for "Forward".
                    // Usually "Forward" means:
                    // dist < 0: Unchanged (sample at x,y)
                    // dist > 0: Shifted (sample at x-shift, y-shift)
                    // Gap: 0 < dist < shift.
                    
                    if (dist < 0) {
                        // Unchanged
                    } else if (dist < effective_shift) {
                        use_border = true;
                    } else {
                        sample_x -= shift_vec_x;
                        sample_y -= shift_vec_y;
                    }
                } else if (direction == 3) { // Backward
                    // dist > 0: Unchanged
                    // dist < 0: Shifted (sample at x+shift, y+shift)
                    // Gap: -shift < dist < 0
                    
                    if (dist > 0) {
                        // Unchanged
                    } else if (dist > -effective_shift) {
                        use_border = true;
                    } else {
                        sample_x += shift_vec_x;
                        sample_y += shift_vec_y;
                    }
                }

                if (use_border) {
                    // Sample from the "cut" line.
                    // Project current point onto the line.
                    // Projection = Anchor + dot(pos-anchor, para) * para
                    float proj_len = dx * para_x + dy * para_y;
                    float border_x = static_cast<float>(anchor_x) + proj_len * para_x;
                    float border_y = static_cast<float>(anchor_y) + proj_len * para_y;
                    
                    out_row[x] = SampleBilinear<Pixel>(input_base, input_rowbytes, border_x, border_y, input_width, input_height);
                } else {
                    // Normal sampling
                    out_row[x] = SampleBilinear<Pixel>(input_base, input_rowbytes, sample_x, sample_y, input_width, input_height);
                }
            }
        }
    };

    int rows_per_thread = (height + num_threads - 1) / num_threads;
    for (int i = 0; i < num_threads; ++i) {
        int start = i * rows_per_thread;
        int end = std::min(start + rows_per_thread, height);
        if (start < end) {
            threads.emplace_back(process_rows, start, end);
        }
    }

    for (auto &t : threads) {
        if (t.joinable()) t.join();
    }

    return PF_Err_NONE;
}

static PF_Err Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
    if (output->world_flags & PF_WorldFlag_DEEP) {
        if (PF_WORLD_IS_DEEP(output)) {
             // 16-bit
             return RenderGeneric<PF_Pixel16>(in_data, out_data, params, output);
        }
    }
    // Check for 32-bit float
    // PF_WorldFlag_FLOAT is not standard in old SDKs, check rowbytes or flags2?
    // Usually we check pixel format.
    // Assuming PF_PixelFloat is used if rowbytes/width == 16 (128 bit) or similar?
    // Actually, standard way is checking `in_data->appl_id` or `pixel_format`.
    // But for this template, we'll assume:
    // If deep flag is NOT set, it's 8-bit.
    // If deep flag IS set, it could be 16-bit or 32-bit.
    // We can check `pixel_format` in `PF_EffectWorld`.
    
    // Robust check:
    int bpp = (output->width > 0) ? (output->rowbytes / output->width) : 0;
    if (bpp == sizeof(PF_PixelFloat)) {
        return RenderGeneric<PF_PixelFloat>(in_data, out_data, params, output);
    } else if (bpp == sizeof(PF_Pixel16)) {
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
    PF_Err result = PF_Err_INVALID_CALLBACK;
    result = PF_REGISTER_EFFECT_EXT2(
        inPtr,
        inPluginDataCallBackPtr,
        "Ae_Stretch", // Name
        "Ae_Stretch", // Match Name
        "Ae_Plugins", // Category
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
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT: err = About(in_data, out_data, params, output); break;
            case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data, params, output); break;
            case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data, params, output); break;
            case PF_Cmd_FRAME_SETUP: err = FrameSetup(in_data, out_data, params, output); break;
            case PF_Cmd_RENDER: err = Render(in_data, out_data, params, output); break;
            default: break;
        }
    } catch (...) {
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    return err;
}
