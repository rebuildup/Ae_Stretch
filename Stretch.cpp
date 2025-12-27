#include "Stretch.h"
#include "AE_EffectCBSuites.h"

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
    
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_THREADED_RENDERING |
                           PF_OutFlag2_REVEALS_ZERO_ALPHA;
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

// Bilinear sampling for anti-aliasing
// This provides smooth edges by interpolating between neighboring pixels
template <typename Pixel>
static inline Pixel SampleBilinear(const A_u_char* base_ptr,
    A_long rowbytes,
    float xf,
    float yf,
    int width,
    int height)
{
    using Traits = PixelTraits<Pixel>;
    
    // Get integer and fractional parts
    int x0 = static_cast<int>(floorf(xf));
    int y0 = static_cast<int>(floorf(yf));
    float fx = xf - static_cast<float>(x0);
    float fy = yf - static_cast<float>(y0);
    
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    
    // Check if all four pixels are within bounds
    bool in_bounds_00 = (x0 >= 0 && x0 < width && y0 >= 0 && y0 < height);
    bool in_bounds_10 = (x1 >= 0 && x1 < width && y0 >= 0 && y0 < height);
    bool in_bounds_01 = (x0 >= 0 && x0 < width && y1 >= 0 && y1 < height);
    bool in_bounds_11 = (x1 >= 0 && x1 < width && y1 >= 0 && y1 < height);
    
    // If all pixels are out of bounds, return transparent
    if (!in_bounds_00 && !in_bounds_10 && !in_bounds_01 && !in_bounds_11) {
        Pixel result;
        std::memset(&result, 0, sizeof(Pixel));
        return result;
    }
    
    // Get pixels (use transparent for out-of-bounds)
    Pixel p00, p10, p01, p11;
    std::memset(&p00, 0, sizeof(Pixel));
    std::memset(&p10, 0, sizeof(Pixel));
    std::memset(&p01, 0, sizeof(Pixel));
    std::memset(&p11, 0, sizeof(Pixel));
    
    if (in_bounds_00) {
        const Pixel* row0 = reinterpret_cast<const Pixel*>(base_ptr + y0 * rowbytes);
        p00 = row0[x0];
    }
    if (in_bounds_10) {
        const Pixel* row0 = reinterpret_cast<const Pixel*>(base_ptr + y0 * rowbytes);
        p10 = row0[x1];
    }
    if (in_bounds_01) {
        const Pixel* row1 = reinterpret_cast<const Pixel*>(base_ptr + y1 * rowbytes);
        p01 = row1[x0];
    }
    if (in_bounds_11) {
        const Pixel* row1 = reinterpret_cast<const Pixel*>(base_ptr + y1 * rowbytes);
        p11 = row1[x1];
    }
    
    // Bilinear interpolation
    float w00 = (1.0f - fx) * (1.0f - fy);
    float w10 = fx * (1.0f - fy);
    float w01 = (1.0f - fx) * fy;
    float w11 = fx * fy;
    
    Pixel result;
    result.alpha = Traits::FromFloat(
        Traits::ToFloat(p00.alpha) * w00 +
        Traits::ToFloat(p10.alpha) * w10 +
        Traits::ToFloat(p01.alpha) * w01 +
        Traits::ToFloat(p11.alpha) * w11
    );
    result.red = Traits::FromFloat(
        Traits::ToFloat(p00.red) * w00 +
        Traits::ToFloat(p10.red) * w10 +
        Traits::ToFloat(p01.red) * w01 +
        Traits::ToFloat(p11.red) * w11
    );
    result.green = Traits::FromFloat(
        Traits::ToFloat(p00.green) * w00 +
        Traits::ToFloat(p10.green) * w10 +
        Traits::ToFloat(p01.green) * w01 +
        Traits::ToFloat(p11.green) * w11
    );
    result.blue = Traits::FromFloat(
        Traits::ToFloat(p00.blue) * w00 +
        Traits::ToFloat(p10.blue) * w10 +
        Traits::ToFloat(p01.blue) * w01 +
        Traits::ToFloat(p11.blue) * w11
    );
    
    return result;
}

// Fast row sampler for cases where Y coordinate is constant across the row
// This avoids repeated Y-coordinate calculations (floor, clamp, row pointer lookup)
template <typename Pixel>
class FastRowSampler {
public:
    const Pixel* row0;
    const Pixel* row1;
    float w0_y; // Weight for row0 (1 - fy)
    float w1_y; // Weight for row1 (fy)
    int width;
    int height;
    
    // Initialize with a constant Y coordinate
    void Setup(const A_u_char* base, A_long rowbytes, int w, int h, float y) {
        width = w;
        height = h;
        
        int y0 = static_cast<int>(floorf(y));
        int y1 = y0 + 1;
        float fy = y - static_cast<float>(y0);
        
        w0_y = 1.0f - fy;
        w1_y = fy;
        
        // Clamp Y and check bounds
        bool y0_in = (y0 >= 0 && y0 < h);
        bool y1_in = (y1 >= 0 && y1 < h);
        
        if (y0_in) row0 = reinterpret_cast<const Pixel*>(base + y0 * rowbytes);
        else row0 = nullptr;
        
        if (y1_in) row1 = reinterpret_cast<const Pixel*>(base + y1 * rowbytes);
        else row1 = nullptr;
    }
    
    // Sample at X coordinate
    inline Pixel Sample(float x) const {
        using Traits = PixelTraits<Pixel>;
        
        int x0 = static_cast<int>(floorf(x));
        int x1 = x0 + 1;
        float fx = x - static_cast<float>(x0);
        
        // Check bounds for X
        bool x0_in = (x0 >= 0 && x0 < width);
        bool x1_in = (x1 >= 0 && x1 < width);
        
        // Weights
        float w0_x = 1.0f - fx;
        float w1_x = fx;
        
        float r = 0, g = 0, b = 0, a = 0;
        
        // Contribution from Row 0
        if (row0) {
            if (x0_in) {
                const Pixel& p = row0[x0];
                float w = w0_y * w0_x;
                a += Traits::ToFloat(p.alpha) * w;
                r += Traits::ToFloat(p.red) * w;
                g += Traits::ToFloat(p.green) * w;
                b += Traits::ToFloat(p.blue) * w;
            }
            if (x1_in) {
                const Pixel& p = row0[x1];
                float w = w0_y * w1_x;
                a += Traits::ToFloat(p.alpha) * w;
                r += Traits::ToFloat(p.red) * w;
                g += Traits::ToFloat(p.green) * w;
                b += Traits::ToFloat(p.blue) * w;
            }
        }
        
        // Contribution from Row 1
        if (row1) {
            if (x0_in) {
                const Pixel& p = row1[x0];
                float w = w1_y * w0_x;
                a += Traits::ToFloat(p.alpha) * w;
                r += Traits::ToFloat(p.red) * w;
                g += Traits::ToFloat(p.green) * w;
                b += Traits::ToFloat(p.blue) * w;
            }
            if (x1_in) {
                const Pixel& p = row1[x1];
                float w = w1_y * w1_x;
                a += Traits::ToFloat(p.alpha) * w;
                r += Traits::ToFloat(p.red) * w;
                g += Traits::ToFloat(p.green) * w;
                b += Traits::ToFloat(p.blue) * w;
            }
        }

        Pixel result;
        result.alpha = Traits::FromFloat(a);
        result.red   = Traits::FromFloat(r);
        result.green = Traits::FromFloat(g);
        result.blue  = Traits::FromFloat(b);
        return result;
    }
};

// Blend two pixels with anti-aliasing
// coverage: 0.0 = fully pixel_a, 1.0 = fully pixel_b
template <typename Pixel>
static inline Pixel BlendPixels(const Pixel& pixel_a, const Pixel& pixel_b, float coverage)
{
    using Traits = PixelTraits<Pixel>;
    
    // Clamp coverage to [0, 1]
    coverage = ClampScalar(coverage, 0.0f, 1.0f);
    float inv_coverage = 1.0f - coverage;
    
    Pixel result;
    result.alpha = Traits::FromFloat(Traits::ToFloat(pixel_a.alpha) * inv_coverage + 
                                     Traits::ToFloat(pixel_b.alpha) * coverage);
    result.red = Traits::FromFloat(Traits::ToFloat(pixel_a.red) * inv_coverage + 
                                   Traits::ToFloat(pixel_b.red) * coverage);
    result.green = Traits::FromFloat(Traits::ToFloat(pixel_a.green) * inv_coverage + 
                                     Traits::ToFloat(pixel_b.green) * coverage);
    result.blue = Traits::FromFloat(Traits::ToFloat(pixel_a.blue) * inv_coverage + 
                                    Traits::ToFloat(pixel_b.blue) * coverage);
    return result;
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
        // Convert output buffer y to input image coordinate system
        const float yf_output = static_cast<float>(y);
        const float yf_input = yf_output - ctx.output_origin_y;
        
        // Calculate distance from anchor point (in input image coordinate system)
        const float dy = yf_input - anchor_y_f;

        // Calculate x range in input image coordinate system
        const float dx0 = 0.0f - ctx.output_origin_x - anchor_x_f;  // Left edge of output buffer in input coords
        const float dxN = static_cast<float>(ctx.width - 1) - ctx.output_origin_x - anchor_x_f;  // Right edge

        const float base_perp = dy * perp_y;
        const float dist0 = dx0 * perp_x + base_perp;
        const float distN = dxN * perp_x + base_perp;

        const float row_min = (std::min)(dist0, distN);
        const float row_max = (std::max)(dist0, distN);

        const float base_para = dy * para_y;

        // sample_x and sample_y are in input image coordinate system
        float sample_x = 0.0f - ctx.output_origin_x;
        const float sample_y = yf_input;

        Pixel* out_row = reinterpret_cast<Pixel*>(ctx.output_base + static_cast<A_long>(y) * ctx.output_rowbytes);

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
            float proj_len = dx0 * para_x + base_para;
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
        float proj_len = dx0 * para_x + base_para;
        
        // Anti-aliasing feather width (in pixels)
        const float feather = 0.5f;
        
        // Pre-calculate constants to avoid repeated computation
        const float eff_plus_feather = eff + feather;
        const float eff_minus_feather = eff - feather;
        const float neg_eff_plus_feather = -eff + feather;
        const float neg_eff_minus_feather = -eff - feather;
        const float feather_inv = 1.0f / (2.0f * feather);

        for (int x = 0; x < ctx.width; ++x) {
            // Pre-calculate border point (used in multiple branches)
            // Note: Border sampling is complex (varying Y), so we use full SampleBilinear -> now SamplePixel
            const float border_x = anchor_x_f + proj_len * para_x;
            const float border_y = anchor_y_f + proj_len * para_y;

            // Determine which region we're in and apply anti-aliasing at boundaries
            if (dist > eff_plus_feather) {
                // Fully in positive shifted region
                const float sx = sample_x - shift_vec_x;
                const float sy = sample_y - shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
            }
            else if (dist < neg_eff_minus_feather) {
                // Fully in negative shifted region
                const float sx = sample_x + shift_vec_x;
                const float sy = sample_y + shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
            }
            else if (dist > eff_minus_feather) {
                // Anti-aliasing zone: transition from gap to positive shifted
                Pixel border_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
                
                const float sx_shifted = sample_x - shift_vec_x;
                const float sy_shifted = sample_y - shift_vec_y;
                Pixel shifted_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx_shifted, sy_shifted, ctx.input_width, ctx.input_height);
                
                // coverage: 0 at (eff - feather), 1 at (eff + feather)
                float coverage = (dist - eff_minus_feather) * feather_inv;
                out_row[x] = BlendPixels(border_pixel, shifted_pixel, coverage);
            }
            else if (dist < neg_eff_plus_feather) {
                // Anti-aliasing zone: transition from negative shifted to gap
                Pixel border_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
                
                const float sx_shifted = sample_x + shift_vec_x;
                const float sy_shifted = sample_y + shift_vec_y;
                Pixel shifted_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx_shifted, sy_shifted, ctx.input_width, ctx.input_height);
                
                // coverage: 1 at (-eff - feather), 0 at (-eff + feather)
                float coverage = (neg_eff_plus_feather - dist) * feather_inv;
                out_row[x] = BlendPixels(border_pixel, shifted_pixel, coverage);
            }
            else {
                // Fully in gap region
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
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
        // Convert output buffer y to input image coordinate system
        const float yf_output = static_cast<float>(y);
        const float yf_input = yf_output - ctx.output_origin_y;
        
        // Calculate distance from anchor point (in input image coordinate system)
        const float dy = yf_input - anchor_y_f;

        // Calculate x range in input image coordinate system
        const float dx0 = 0.0f - ctx.output_origin_x - anchor_x_f;  // Left edge of output buffer in input coords
        const float dxN = static_cast<float>(ctx.width - 1) - ctx.output_origin_x - anchor_x_f;  // Right edge

        const float base_perp = dy * perp_y;
        const float dist0 = dx0 * perp_x + base_perp;
        const float distN = dxN * perp_x + base_perp;

        const float row_min = (std::min)(dist0, distN);
        const float row_max = (std::max)(dist0, distN);

        Pixel* out_row = reinterpret_cast<Pixel*>(ctx.output_base + static_cast<A_long>(y) * ctx.output_rowbytes);
        const Pixel* in_row = reinterpret_cast<const Pixel*>(ctx.input_base + static_cast<A_long>(y) * ctx.input_rowbytes);

        const float base_para = dy * para_y;

        // sample_x and sample_y are in input image coordinate system
        float sample_x = 0.0f - ctx.output_origin_x;
        const float sample_y = yf_input;

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
            float proj_len = dx0 * para_x + base_para;
            for (int x = 0; x < ctx.width; ++x) {
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
                proj_len += para_x;
            }
            continue;
        }

        // General case
        // General case
        float dist = dist0;
        float proj_len = dx0 * para_x + base_para;

        // Anti-aliasing constants
        const float feather = 0.5f;
        const float eff_plus_feather = eff + feather;
        const float eff_minus_feather = eff - feather;
        const float feather_inv = 1.0f / (2.0f * feather);

        for (int x = 0; x < ctx.width; ++x) {
            // Pre-calculate border point (used in multiple branches)
            const float border_x = anchor_x_f + proj_len * para_x;
            const float border_y = anchor_y_f + proj_len * para_y;

            if (dist < -feather) {
                // Unchanged
                out_row[x] = in_row[x];
            }
            else if (dist > eff_plus_feather) {
                // Shifted
                const float sx = sample_x - shift_vec_x;
                const float sy = sample_y - shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
            }
            else if (dist <= feather) {
                // Anti-aliasing zone: transition from original to gap (around dist=0)
                // dist is in [-feather, feather]
                Pixel border_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
                
                // coverage: 0 at feather (Original), 1 at -feather (Border).
                // Wait, logic: dist < 0 is original.
                // t should be 0 at dist = -feather, 1 at dist = feather.
                // Original logic in step 364: 
                // float t = (dist - (-feather)) * feather_inv;
                // out_row[x] = BlendPixels(in_row[x], border_pixel, t);
                // Correct.

                float t = (dist + feather) * feather_inv;
                out_row[x] = BlendPixels(in_row[x], border_pixel, t);
            }
            else if (dist > eff_minus_feather) {
                // Anti-aliasing zone: transition from gap to shifted (around dist=eff)
                Pixel border_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
                
                const float sx_shifted = sample_x - shift_vec_x;
                const float sy_shifted = sample_y - shift_vec_y;
                Pixel shifted_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx_shifted, sy_shifted, ctx.input_width, ctx.input_height);
                
                // coverage: 0 at eff-feather (Border), 1 at eff+feather (Shifted)
                float t = (dist - eff_minus_feather) * feather_inv;
                out_row[x] = BlendPixels(border_pixel, shifted_pixel, t);
            }
            else {
                // Purely Border (Gap)
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
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
        // Convert output buffer y to input image coordinate system
        const float yf_output = static_cast<float>(y);
        const float yf_input = yf_output - ctx.output_origin_y;
        
        // Calculate distance from anchor point (in input image coordinate system)
        const float dy = yf_input - anchor_y_f;

        // Calculate x range in input image coordinate system
        const float dx0 = 0.0f - ctx.output_origin_x - anchor_x_f;  // Left edge of output buffer in input coords
        const float dxN = static_cast<float>(ctx.width - 1) - ctx.output_origin_x - anchor_x_f;  // Right edge

        const float base_perp = dy * perp_y;
        const float dist0 = dx0 * perp_x + base_perp;
        const float distN = dxN * perp_x + base_perp;

        const float row_min = (std::min)(dist0, distN);
        const float row_max = (std::max)(dist0, distN);

        Pixel* out_row = reinterpret_cast<Pixel*>(ctx.output_base + static_cast<A_long>(y) * ctx.output_rowbytes);
        const Pixel* in_row = reinterpret_cast<const Pixel*>(ctx.input_base + static_cast<A_long>(y) * ctx.input_rowbytes);

        const float base_para = dy * para_y;

        // sample_x and sample_y are in input image coordinate system
        float sample_x = 0.0f - ctx.output_origin_x;
        const float sample_y = yf_input;

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
            float proj_len = dx0 * para_x + base_para;
            for (int x = 0; x < ctx.width; ++x) {
                const float border_x = anchor_x_f + proj_len * para_x;
                const float border_y = anchor_y_f + proj_len * para_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
                proj_len += para_x;
            }
            continue;
        }

        // General case
        // General case
        float dist = dist0;
        float proj_len = dx0 * para_x + base_para;

        // Anti-aliasing constants
        const float feather = 0.5f;
        const float neg_eff_plus_feather = -eff + feather;
        const float neg_eff_minus_feather = -eff - feather;
        const float feather_inv = 1.0f / (2.0f * feather);

        for (int x = 0; x < ctx.width; ++x) {
            // Pre-calculate border point
            const float border_x = anchor_x_f + proj_len * para_x;
            const float border_y = anchor_y_f + proj_len * para_y;

            if (dist > feather) {
                // Unchanged
                out_row[x] = in_row[x];
            }
            else if (dist < neg_eff_minus_feather) {
                // Shifted
                const float sx = sample_x + shift_vec_x;
                const float sy = sample_y + shift_vec_y;
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx, sy, ctx.input_width, ctx.input_height);
            }
            else if (dist >= -feather) {
                // Anti-aliasing zone: transition from border to original (around dist=0)
                // dist is in [-feather, feather]
                Pixel border_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
                
                // coverage: 0 at feather (Original), 1 at -feather (Border)
                float t = (feather - dist) * feather_inv;
                out_row[x] = BlendPixels(in_row[x], border_pixel, t);
            }
            else if (dist < neg_eff_plus_feather) {
                // Anti-aliasing zone: transition from shifted to border (around dist=-eff)
                // dist is in [-eff-feather, -eff+feather]
                Pixel border_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
                
                const float sx_shifted = sample_x + shift_vec_x;
                const float sy_shifted = sample_y + shift_vec_y;
                Pixel shifted_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sx_shifted, sy_shifted, ctx.input_width, ctx.input_height);
                
                // coverage: 0 at -eff+feather (Border), 1 at -eff-feather (Shifted)
                float t = (neg_eff_plus_feather - dist) * feather_inv;
                out_row[x] = BlendPixels(border_pixel, shifted_pixel, t);
            }
            else {
                // Purely Border (Gap)
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, border_x, border_y, ctx.input_width, ctx.input_height);
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

    // Anchor point is in input image coordinate system
    const float anchor_x_f = static_cast<float>(anchor_x);
    const float anchor_y_f = static_cast<float>(anchor_y);

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

    // Use main thread only to avoid issues with AE API calls from std::thread
    if (direction == 1) {
        ProcessRowsBoth(ctx, 0, height);
    }
    else if (direction == 2) {
        ProcessRowsForward(ctx, 0, height);
    }
    else {
        ProcessRowsBackward(ctx, 0, height);
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

