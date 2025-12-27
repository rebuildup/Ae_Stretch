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
About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
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
GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    (void)in_data;
    (void)params;
    (void)output;

    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    
    // Enable expanding output buffer beyond layer bounds
    // This allows the effect to render pixels outside the original layer boundaries
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE | 
                          PF_OutFlag_PIX_INDEPENDENT |
                          PF_OutFlag_I_EXPAND_BUFFER;
    
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
    return PF_Err_NONE;
}

static PF_Err
FrameSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    (void)output;
    
    PF_Err err = PF_Err_NONE;
    
    // Get input dimensions
    PF_LayerDef* input = &params[STRETCH_INPUT]->u.ld;
    const int input_width = input->width;
    const int input_height = input->height;
    
    if (input_width <= 0 || input_height <= 0) {
        return PF_Err_NONE;
    }
    
    // Get parameters
    const float shift_amount = static_cast<float>(params[STRETCH_SHIFT_AMOUNT]->u.fs_d.value);
    float angle_deg = static_cast<float>(params[STRETCH_ANGLE]->u.ad.value >> 16);
    const int direction = params[STRETCH_DIRECTION]->u.pd.value;
    
    // Downsample adjustment
    const float downsample_x = static_cast<float>(in_data->downsample_x.den) / static_cast<float>(in_data->downsample_x.num);
    const float downsample_y = static_cast<float>(in_data->downsample_y.den) / static_cast<float>(in_data->downsample_y.num);
    const float downsample = std::min(downsample_x, downsample_y);
    
    // Effective shift in pixels
    float effective_shift = (downsample > 0.0f) ? (shift_amount / downsample) : shift_amount;
    
    // If no shift, no expansion needed
    if (std::abs(effective_shift) < 0.01f) {
        return PF_Err_NONE;
    }
    
    // Direction adjustment (Both mode splits the shift)
    if (direction == 1) { // Both
        effective_shift *= 0.5f;
    }
    
    // Calculate shift vector from angle
    const float angle_rad = angle_deg * (static_cast<float>(M_PI) / 180.0f);
    const float sn = std::sin(angle_rad);
    const float cs = std::cos(angle_rad);
    
    // Perpendicular vector (direction of shift)
    const float perp_x = -sn;
    const float perp_y = cs;
    
    // Calculate maximum shift vector
    const float shift_vec_x = perp_x * effective_shift;
    const float shift_vec_y = perp_y * effective_shift;
    
    // Calculate bounding box for all possible shifted positions
    // We need to consider the four corners of the input image
    // and their positions after maximum shift in both directions
    
    float min_x = 0.0f;
    float max_x = static_cast<float>(input_width);
    float min_y = 0.0f;
    float max_y = static_cast<float>(input_height);
    
    // Corner points
    const float corners[4][2] = {
        {0.0f, 0.0f},
        {static_cast<float>(input_width), 0.0f},
        {0.0f, static_cast<float>(input_height)},
        {static_cast<float>(input_width), static_cast<float>(input_height)}
    };
    
    // For each corner, calculate its position after maximum shift
    for (int i = 0; i < 4; i++) {
        const float x = corners[i][0];
        const float y = corners[i][1];
        
        // Depending on direction mode, calculate possible positions
        if (direction == 1) { // Both - can shift in both directions
            // Positive shift
            float x_pos = x + shift_vec_x;
            float y_pos = y + shift_vec_y;
            // Negative shift
            float x_neg = x - shift_vec_x;
            float y_neg = y - shift_vec_y;
            
            min_x = std::min({min_x, x_pos, x_neg});
            max_x = std::max({max_x, x_pos, x_neg});
            min_y = std::min({min_y, y_pos, y_neg});
            max_y = std::max({max_y, y_pos, y_neg});
        }
        else if (direction == 2) { // Forward - shift in negative direction
            float x_shifted = x - shift_vec_x;
            float y_shifted = y - shift_vec_y;
            
            min_x = std::min(min_x, x_shifted);
            max_x = std::max(max_x, x_shifted);
            min_y = std::min(min_y, y_shifted);
            max_y = std::max(max_y, y_shifted);
        }
        else { // Backward - shift in positive direction
            float x_shifted = x + shift_vec_x;
            float y_shifted = y + shift_vec_y;
            
            min_x = std::min(min_x, x_shifted);
            max_x = std::max(max_x, x_shifted);
            min_y = std::min(min_y, y_shifted);
            max_y = std::max(max_y, y_shifted);
        }
    }
    
    // Calculate required expansion
    int expand_left = static_cast<int>(std::ceil(-min_x));
    int expand_top = static_cast<int>(std::ceil(-min_y));
    int expand_right = static_cast<int>(std::ceil(max_x - input_width));
    int expand_bottom = static_cast<int>(std::ceil(max_y - input_height));
    
    // Ensure non-negative
    if (expand_left < 0) expand_left = 0;
    if (expand_top < 0) expand_top = 0;
    if (expand_right < 0) expand_right = 0;
    if (expand_bottom < 0) expand_bottom = 0;
    
    // Set output dimensions and origin
    out_data->width = input_width + expand_left + expand_right;
    out_data->height = input_height + expand_top + expand_bottom;
    out_data->origin.h = static_cast<short>(expand_left);
    out_data->origin.v = static_cast<short>(expand_top);
    
    return err;
}


static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
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

// Nearest Neighbor sampling to avoid black fringe artifacts
// This method prevents blending with transparent (alpha=0) black pixels,
// which is the recommended approach per Adobe's documentation:
// https://ae-plugins.docsforadobe.dev/effect-details/pixel-aspect-ratio/?h=anti+aliasing#dont-assume-pixels-are-square-or-1-to-1
template <typename Pixel>
static inline Pixel SampleNearestNeighbor(const A_u_char* base_ptr,
    A_long rowbytes,
    float xf,
    float yf,
    int width,
    int height)
{
    // Round to nearest integer (same method as MultiSlicer)
    A_long x = static_cast<A_long>(xf + 0.5f);
    A_long y = static_cast<A_long>(yf + 0.5f);

    // Bounds check
    if (x < 0 || x >= width || y < 0 || y >= height) {
        Pixel result;
        std::memset(&result, 0, sizeof(Pixel));
        return result;
    }

    const Pixel* row = reinterpret_cast<const Pixel*>(base_ptr + y * rowbytes);
    return row[x];
}

// -----------------------------------------------------------------------------
// Stretch rendering helpers
// -----------------------------------------------------------------------------

template <typename Pixel>
struct StretchRenderContext
{
    const A_u_char* input_base;
    A_u_char* output_base;
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
    
    // Output origin offset (for expanded buffer)
    float output_origin_x;
    float output_origin_y;
};

template <typename Pixel>
static inline void ProcessRowsBoth(const StretchRenderContext<Pixel>& ctx, int start_y, int end_y)
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

        // Convert output buffer coordinates to input image coordinates
        // Output buffer (0,0) corresponds to input image (-output_origin_x, -output_origin_y)
        float sample_x = 0.0f - ctx.output_origin_x;
        const float sample_y = yf - ctx.output_origin_y;

        Pixel* out_row = reinterpret_cast<Pixel*>(ctx.output_base + static_cast<A_long>(y) * ctx.output_rowbytes);

        // Entire row is on the negative side beyond the gap -> all pixels shift in +direction
        if (row_max <= -eff) {
            for (int x = 0; x < ctx.width; ++x) {
                const float sx = sample_x + shift_vec_x;
                const float sy = sample_y + shift_vec_y;
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
                sample_x += 1.0f;
            }
            continue;
        }

        // Entire row is on the positive side beyond the gap -> all pixels shift in -direction
        if (row_min >= eff) {
            for (int x = 0; x < ctx.width; ++x) {
                const float sx = sample_x - shift_vec_x;
                const float sy = sample_y - shift_vec_y;
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
                sample_x += 1.0f;
            }
            continue;
        }

        // Entire row is inside the gap -> border sampling only
        if (row_min > -eff && row_max < eff) {
            for (int x = 0; x < ctx.width; ++x) {
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
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
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
            }
            else if (dist < -eff) {
                sx += shift_vec_x;
                sy += shift_vec_y;
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
            }
            else {
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
            }

            sample_x += 1.0f;
            dist += perp_x;
            proj_len += para_x;
        }
    }
}

template <typename Pixel>
static inline void ProcessRowsForward(const StretchRenderContext<Pixel>& ctx, int start_y, int end_y)
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

        Pixel* out_row = reinterpret_cast<Pixel*>(ctx.output_base + static_cast<A_long>(y) * ctx.output_rowbytes);
        const Pixel* in_row = reinterpret_cast<const Pixel*>(ctx.input_base + static_cast<A_long>(y) * ctx.input_rowbytes);

        const float base_para = dy * para_y;
        float proj_len = dx0 * para_x + base_para;

        // Convert output buffer coordinates to input image coordinates
        float sample_x = 0.0f - ctx.output_origin_x;
        const float sample_y = yf - ctx.output_origin_y;

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
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
                sample_x += 1.0f;
            }
            continue;
        }

        // Entire row is within gap: 0 <= dist < eff -> border only
        if (row_min >= 0.0f && row_max < eff) {
            for (int x = 0; x < ctx.width; ++x) {
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
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
            }
            else if (dist < eff) {
                // Border
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
            }
            else {
                // Shifted
                const float sx = sample_x - shift_vec_x;
                const float sy = sample_y - shift_vec_y;
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
            }

            sample_x += 1.0f;
            dist += perp_x;
            proj_len += para_x;
        }
    }
}

template <typename Pixel>
static inline void ProcessRowsBackward(const StretchRenderContext<Pixel>& ctx, int start_y, int end_y)
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

        Pixel* out_row = reinterpret_cast<Pixel*>(ctx.output_base + static_cast<A_long>(y) * ctx.output_rowbytes);
        const Pixel* in_row = reinterpret_cast<const Pixel*>(ctx.input_base + static_cast<A_long>(y) * ctx.input_rowbytes);

        const float base_para = dy * para_y;
        float proj_len = dx0 * para_x + base_para;

        // Convert output buffer coordinates to input image coordinates
        float sample_x = 0.0f - ctx.output_origin_x;
        const float sample_y = yf - ctx.output_origin_y;

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
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
                sample_x += 1.0f;
            }
            continue;
        }

        // Entire row is within gap: -eff < dist <= 0 -> border only
        if (row_min > -eff && row_max <= 0.0f) {
            for (int x = 0; x < ctx.width; ++x) {
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
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
            }
            else if (dist > -eff) {
                // Border
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
            }
            else {
                // Shifted
                const float sx = sample_x + shift_vec_x;
                const float sy = sample_y + shift_vec_y;
                out_row[x] = SampleNearestNeighbor<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
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
static PF_Err RenderGeneric(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    (void)out_data;

    PF_EffectWorld* input = &params[STRETCH_INPUT]->u.ld;

    const int width = output->width;
    const int height = output->height;
    const int input_width = input->width;
    const int input_height = input->height;

    if (width <= 0 || height <= 0) {
        return PF_Err_NONE;
    }

    const A_u_char* input_base = reinterpret_cast<const A_u_char*>(input->data);
    A_u_char* output_base = reinterpret_cast<A_u_char*>(output->data);
    const A_long input_rowbytes = input->rowbytes;
    const A_long output_rowbytes = output->rowbytes;

    // Parameters
    // When PF_OutFlag_I_EXPAND_BUFFER is set, After Effects automatically adjusts point parameters
    // to the expanded buffer coordinate system. We need to convert back to input image coordinates.
    const int anchor_x_raw = (params[STRETCH_ANCHOR_POINT]->u.td.x_value >> 16);
    const int anchor_y_raw = (params[STRETCH_ANCHOR_POINT]->u.td.y_value >> 16);
    
    // Convert from expanded buffer coordinates to input image coordinates
    const int anchor_x = anchor_x_raw - in_data->pre_effect_source_origin_x;
    const int anchor_y = anchor_y_raw - in_data->pre_effect_source_origin_y;
    
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

    // Convert anchor point from input image coordinate system to output buffer coordinate system
    // Input image (0,0) is at output buffer (output_origin_x, output_origin_y)
    const float anchor_x_f = static_cast<float>(anchor_x) + static_cast<float>(in_data->output_origin_x);
    const float anchor_y_f = static_cast<float>(anchor_y) + static_cast<float>(in_data->output_origin_y);

    StretchRenderContext<Pixel> ctx{};
    ctx.input_base = input_base;
    ctx.output_base = output_base;
    ctx.input_rowbytes = input_rowbytes;
    ctx.output_rowbytes = output_rowbytes;
    ctx.width = width;
    ctx.height = height;
    ctx.input_width = input_width;
    ctx.input_height = input_height;
    ctx.anchor_x = anchor_x_f;
    ctx.anchor_y = anchor_y_f;
    ctx.effective_shift = effective_shift;
    ctx.shift_vec_x = shift_vec_x;
    ctx.shift_vec_y = shift_vec_y;
    ctx.perp_x = perp_x;
    ctx.perp_y = perp_y;
    ctx.para_x = para_x;
    ctx.para_y = para_y;
    ctx.output_origin_x = static_cast<float>(in_data->output_origin_x);
    ctx.output_origin_y = static_cast<float>(in_data->output_origin_y);

    const int max_threads = std::max(1u, std::thread::hardware_concurrency());
    const int height_clamped = std::max(height, 1);
    const int num_threads = std::min(max_threads, height_clamped);
    const int rows_per_thread = (height_clamped + num_threads - 1) / num_threads;

    auto worker = [&](int start_y, int end_y) {
        if (direction == 1) {
            ProcessRowsBoth(ctx, start_y, end_y);
        }
        else if (direction == 2) {
            ProcessRowsForward(ctx, start_y, end_y);
        }
        else {
            ProcessRowsBackward(ctx, start_y, end_y);
        }
        };

    if (num_threads <= 1 || height <= 1) {
        worker(0, height);
    }
    else {
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
        for (auto& th : threads) {
            th.join();
        }
    }

    return PF_Err_NONE;
}

static PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
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
    }
    else if (bpp == static_cast<int>(sizeof(PF_Pixel16))) {
        return RenderGeneric<PF_Pixel16>(in_data, out_data, params, output);
    }
    else {
        return RenderGeneric<PF_Pixel>(in_data, out_data, params, output);
    }
}

extern "C" DllExport
PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr,
    PF_PluginDataCB2 inPluginDataCallBackPtr,
    SPBasicSuite* inSPBasicSuitePtr,
    const char* inHostName,
    const char* inHostVersion)
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
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra)
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
    }
    catch (...) {
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    return err;
}

