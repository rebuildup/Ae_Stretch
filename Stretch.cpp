#include "Stretch.h"
#include "AE_EffectCBSuites.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <thread>
#include <cstring>
#include <atomic>

// -----------------------------------------------------------------------------
// UI / boilerplate
// -----------------------------------------------------------------------------

static PF_Err
About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    (void)params;
    (void)output;

    // Null pointer check before dereferencing suites
    if (!in_data || !out_data) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    AEGP_SuiteHandler suites(in_data->pica_basicP);
    auto* ansi_suite = suites.ANSICallbacksSuite1();
    if (!ansi_suite) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    ansi_suite->sprintf(out_data->return_msg,
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

    // Null pointer checks
    if (!in_data || !params || !params[STRETCH_INPUT]) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

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

    // Downsample adjustment with division by zero protection (check both num > 0 and den != 0)
    const float downsample_x = (in_data->downsample_x.num > 0 && in_data->downsample_x.den != 0)
        ? static_cast<float>(in_data->downsample_x.den) / static_cast<float>(in_data->downsample_x.num)
        : 1.0f;
    const float downsample_y = (in_data->downsample_y.num > 0 && in_data->downsample_y.den != 0)
        ? static_cast<float>(in_data->downsample_y.den) / static_cast<float>(in_data->downsample_y.num)
        : 1.0f;
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
    
    // Calculate bounding box
    float min_x = 0.0f;
    float max_x = static_cast<float>(input_width);
    float min_y = 0.0f;
    float max_y = static_cast<float>(input_height);
    
    const float corners[4][2] = {
        {0.0f, 0.0f},
        {static_cast<float>(input_width), 0.0f},
        {0.0f, static_cast<float>(input_height)},
        {static_cast<float>(input_width), static_cast<float>(input_height)}
    };
    
    for (int i = 0; i < 4; i++) {
        const float x = corners[i][0];
        const float y = corners[i][1];
        
        if (direction == 1) { // Both
            float x_pos = x + shift_vec_x;
            float y_pos = y + shift_vec_y;
            float x_neg = x - shift_vec_x;
            float y_neg = y - shift_vec_y;
            
            min_x = std::min({min_x, x_pos, x_neg});
            max_x = std::max({max_x, x_pos, x_neg});
            min_y = std::min({min_y, y_pos, y_neg});
            max_y = std::max({max_y, y_pos, y_neg});
        }
        else if (direction == 2) { // Forward
            // Forward: pixels shift in -shift_vec direction (sampling from -shift_vec)
            // So the image appears to move in +shift_vec direction
            // We need to expand buffer in +shift_vec direction
            float x_shifted = x + shift_vec_x;
            float y_shifted = y + shift_vec_y;
            
            min_x = std::min(min_x, x_shifted);
            max_x = std::max(max_x, x_shifted);
            min_y = std::min(min_y, y_shifted);
            max_y = std::max(max_y, y_shifted);
        }
        else { // Backward (direction == 3)
            // Backward: pixels shift in +shift_vec direction (sampling from +shift_vec)
            // So the image appears to move in -shift_vec direction
            // We need to expand buffer in -shift_vec direction
            float x_shifted = x - shift_vec_x;
            float y_shifted = y - shift_vec_y;
            
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

    if (expand_left < 0) expand_left = 0;
    if (expand_top < 0) expand_top = 0;
    if (expand_right < 0) expand_right = 0;
    if (expand_bottom < 0) expand_bottom = 0;

    // Set output dimensions and origin with integer overflow protection
    // Clamp to short range (-32768 to 32767) to prevent overflow when casting to short
    constexpr int short_max = 32767;
    const int clamped_expand_left = std::min(expand_left, short_max);
    const int clamped_expand_top = std::min(expand_top, short_max);

    out_data->width = input_width + expand_left + expand_right;
    out_data->height = input_height + expand_top + expand_bottom;
    out_data->origin.h = static_cast<short>(clamped_expand_left);
    out_data->origin.v = static_cast<short>(clamped_expand_top);
    
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

// Alpha-weighted bilinear sampling for proper anti-aliasing with transparency
// This avoids black fringing by excluding transparent pixels from interpolation
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
    const int x0 = static_cast<int>(floorf(xf));
    const int y0 = static_cast<int>(floorf(yf));
    const float fx = xf - static_cast<float>(x0);
    const float fy = yf - static_cast<float>(y0);
    
    // Fast path: if coordinate is (nearly) integer, skip bilinear interpolation
    if (fx < EPSILON && fy < EPSILON) {
        // Check bounds
        if (x0 >= 0 && x0 < width && y0 >= 0 && y0 < height) {
            const Pixel* row = reinterpret_cast<const Pixel*>(base_ptr + y0 * rowbytes);
            return row[x0];
        }
        // Out of bounds - return transparent
        Pixel result;
        std::memset(&result, 0, sizeof(Pixel));
        return result;
    }
    
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    
    // Check if all four pixels are within bounds
    const bool in_bounds_00 = (x0 >= 0 && x0 < width && y0 >= 0 && y0 < height);
    const bool in_bounds_10 = (x1 >= 0 && x1 < width && y0 >= 0 && y0 < height);
    const bool in_bounds_01 = (x0 >= 0 && x0 < width && y1 >= 0 && y1 < height);
    const bool in_bounds_11 = (x1 >= 0 && x1 < width && y1 >= 0 && y1 < height);
    
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
    
    // Bilinear weights - pre-compute (1-fx) and (1-fy) to avoid redundant subtraction
    const float inv_fx = 1.0f - fx;
    const float inv_fy = 1.0f - fy;
    const float w00 = inv_fx * inv_fy;
    const float w10 = fx * inv_fy;
    const float w01 = inv_fx * fy;
    const float w11 = fx * fy;
    
    // Get alpha values for weighting - convert once and reuse
    const float a00 = Traits::ToFloat(p00.alpha);
    const float a10 = Traits::ToFloat(p10.alpha);
    const float a01 = Traits::ToFloat(p01.alpha);
    const float a11 = Traits::ToFloat(p11.alpha);
    
    // Alpha-weighted interpolation
    // Pixels with zero or near-zero alpha don't contribute to color
    
    float total_weight = 0.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    
    // Process each pixel - convert RGB values only when alpha is significant
    if (a00 > ALPHA_THRESHOLD) {
        const float weight = w00 * a00;
        total_weight += weight;
        r += Traits::ToFloat(p00.red) * weight;
        g += Traits::ToFloat(p00.green) * weight;
        b += Traits::ToFloat(p00.blue) * weight;
        a += a00 * w00;
    }
    
    if (a10 > ALPHA_THRESHOLD) {
        const float weight = w10 * a10;
        total_weight += weight;
        r += Traits::ToFloat(p10.red) * weight;
        g += Traits::ToFloat(p10.green) * weight;
        b += Traits::ToFloat(p10.blue) * weight;
        a += a10 * w10;
    }
    
    if (a01 > ALPHA_THRESHOLD) {
        const float weight = w01 * a01;
        total_weight += weight;
        r += Traits::ToFloat(p01.red) * weight;
        g += Traits::ToFloat(p01.green) * weight;
        b += Traits::ToFloat(p01.blue) * weight;
        a += a01 * w01;
    }
    
    if (a11 > ALPHA_THRESHOLD) {
        const float weight = w11 * a11;
        total_weight += weight;
        r += Traits::ToFloat(p11.red) * weight;
        g += Traits::ToFloat(p11.green) * weight;
        b += Traits::ToFloat(p11.blue) * weight;
        a += a11 * w11;
    }
    
    Pixel result;
    
    if (total_weight > ALPHA_THRESHOLD) {
        // Normalize by total weight - use multiplication by inverse instead of division
        const float inv_weight = 1.0f / total_weight;
        result.red = Traits::FromFloat(r * inv_weight);
        result.green = Traits::FromFloat(g * inv_weight);
        result.blue = Traits::FromFloat(b * inv_weight);
        result.alpha = Traits::FromFloat(a);
    } else {
        // All pixels were transparent
        std::memset(&result, 0, sizeof(Pixel));
    }
    
    return result;
}

// Fast row sampler for cases where Y coordinate is constant across the row
// This avoids repeated Y-coordinate calculations (floor, clamp, row pointer lookup)
// Uses alpha-weighted interpolation to avoid black fringing with transparent pixels
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
        
        const int y0 = static_cast<int>(floorf(y));
        const int y1 = y0 + 1;
        const float fy = y - static_cast<float>(y0);
        
        w0_y = 1.0f - fy;
        w1_y = fy;
        
        // Clamp Y and check bounds
        const bool y0_in = (y0 >= 0 && y0 < h);
        const bool y1_in = (y1 >= 0 && y1 < h);
        
        row0 = y0_in ? reinterpret_cast<const Pixel*>(base + y0 * rowbytes) : nullptr;
        row1 = y1_in ? reinterpret_cast<const Pixel*>(base + y1 * rowbytes) : nullptr;
    }
    
    // Sample at X coordinate with alpha-weighted interpolation
    inline Pixel Sample(float x) const {
        using Traits = PixelTraits<Pixel>;

        const int x0 = static_cast<int>(floorf(x));
        const float fx = x - static_cast<float>(x0);

        // Fast path: if X is (nearly) integer and Y weight is heavily on one row
        if (fx < EPSILON) {
            if (x0 >= 0 && x0 < width) {
                // Check if we can use single row (when fy was near 0 or 1)
                if (row0 && w0_y > WEIGHT_THRESHOLD) {
                    return row0[x0];
                }
                if (row1 && w1_y > WEIGHT_THRESHOLD) {
                    return row1[x0];
                }
            }
        }
        
        const int x1 = x0 + 1;
        const float inv_fx = 1.0f - fx;
        
        // Check bounds for X
        const bool x0_in = (x0 >= 0 && x0 < width);
        const bool x1_in = (x1 >= 0 && x1 < width);
        
        // If completely out of bounds, return transparent
        if (!row0 && !row1) {
            Pixel result;
            std::memset(&result, 0, sizeof(Pixel));
            return result;
        }
        
        float total_weight = 0.0f;
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
        
        // Contribution from Row 0
        if (row0) {
            if (x0_in) {
                const Pixel& p = row0[x0];
                const float pa = Traits::ToFloat(p.alpha);
                if (pa > ALPHA_THRESHOLD) {
                    const float w = w0_y * inv_fx;
                    const float weight = w * pa;
                    total_weight += weight;
                    r += Traits::ToFloat(p.red) * weight;
                    g += Traits::ToFloat(p.green) * weight;
                    b += Traits::ToFloat(p.blue) * weight;
                    a += pa * w;
                }
            }
            if (x1_in) {
                const Pixel& p = row0[x1];
                const float pa = Traits::ToFloat(p.alpha);
                if (pa > ALPHA_THRESHOLD) {
                    const float w = w0_y * fx;
                    const float weight = w * pa;
                    total_weight += weight;
                    r += Traits::ToFloat(p.red) * weight;
                    g += Traits::ToFloat(p.green) * weight;
                    b += Traits::ToFloat(p.blue) * weight;
                    a += pa * w;
                }
            }
        }
        
        // Contribution from Row 1
        if (row1) {
            if (x0_in) {
                const Pixel& p = row1[x0];
                const float pa = Traits::ToFloat(p.alpha);
                if (pa > ALPHA_THRESHOLD) {
                    const float w = w1_y * inv_fx;
                    const float weight = w * pa;
                    total_weight += weight;
                    r += Traits::ToFloat(p.red) * weight;
                    g += Traits::ToFloat(p.green) * weight;
                    b += Traits::ToFloat(p.blue) * weight;
                    a += pa * w;
                }
            }
            if (x1_in) {
                const Pixel& p = row1[x1];
                const float pa = Traits::ToFloat(p.alpha);
                if (pa > ALPHA_THRESHOLD) {
                    const float w = w1_y * fx;
                    const float weight = w * pa;
                    total_weight += weight;
                    r += Traits::ToFloat(p.red) * weight;
                    g += Traits::ToFloat(p.green) * weight;
                    b += Traits::ToFloat(p.blue) * weight;
                    a += pa * w;
                }
            }
        }

        Pixel result;
        if (total_weight > ALPHA_THRESHOLD) {
            const float inv_weight = 1.0f / total_weight;
            result.red = Traits::FromFloat(r * inv_weight);
            result.green = Traits::FromFloat(g * inv_weight);
            result.blue = Traits::FromFloat(b * inv_weight);
            result.alpha = Traits::FromFloat(a);
        } else {
            std::memset(&result, 0, sizeof(Pixel));
        }
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
            const float sy = sample_y + shift_vec_y;
            FastRowSampler<Pixel> sampler;
            sampler.Setup(ctx.input_base, ctx.input_rowbytes, ctx.input_width, ctx.input_height, sy);
            for (int x = 0; x < ctx.width; ++x) {
                const float sx = sample_x + shift_vec_x;
                out_row[x] = sampler.Sample(sx);
                sample_x += 1.0f;
            }
            continue;
        }

        // Entire row is on the positive side beyond the gap -> all pixels shift in -direction
        if (row_min >= eff) {
            const float sy = sample_y - shift_vec_y;
            FastRowSampler<Pixel> sampler;
            sampler.Setup(ctx.input_base, ctx.input_rowbytes, ctx.input_width, ctx.input_height, sy);
            for (int x = 0; x < ctx.width; ++x) {
                const float sx = sample_x - shift_vec_x;
                out_row[x] = sampler.Sample(sx);
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
        const float feather = FEATHER_AMOUNT;
        
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

        const float base_para = dy * para_y;

        // sample_x and sample_y are in input image coordinate system
        float sample_x = 0.0f - ctx.output_origin_x;
        const float sample_y = yf_input;

        // Entire row is fully shifted (dist >= eff)
        if (row_min >= eff) {
            const float sy = sample_y - shift_vec_y;
            FastRowSampler<Pixel> sampler;
            sampler.Setup(ctx.input_base, ctx.input_rowbytes, ctx.input_width, ctx.input_height, sy);
            for (int x = 0; x < ctx.width; ++x) {
                const float sx = sample_x - shift_vec_x;
                out_row[x] = sampler.Sample(sx);
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

        // General case - always process pixel by pixel
        float dist = dist0;
        float proj_len = dx0 * para_x + base_para;

        // Anti-aliasing constants
        const float feather = FEATHER_AMOUNT;
        const float eff_plus_feather = eff + feather;
        const float eff_minus_feather = eff - feather;
        const float feather_inv = 1.0f / (2.0f * feather);

        for (int x = 0; x < ctx.width; ++x) {
            // Pre-calculate border point (used in multiple branches)
            const float border_x = anchor_x_f + proj_len * para_x;
            const float border_y = anchor_y_f + proj_len * para_y;

            if (dist < -feather) {
                // Unchanged - sample from original position
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sample_x, sample_y, ctx.input_width, ctx.input_height);
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
                Pixel original_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sample_x, sample_y, ctx.input_width, ctx.input_height);
                
                float t = (dist + feather) * feather_inv;
                out_row[x] = BlendPixels(original_pixel, border_pixel, t);
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

        const float base_para = dy * para_y;

        // sample_x and sample_y are in input image coordinate system
        float sample_x = 0.0f - ctx.output_origin_x;
        const float sample_y = yf_input;

        // Entire row is fully shifted (dist <= -eff)
        if (row_max <= -eff) {
            const float sy = sample_y + shift_vec_y;
            FastRowSampler<Pixel> sampler;
            sampler.Setup(ctx.input_base, ctx.input_rowbytes, ctx.input_width, ctx.input_height, sy);
            for (int x = 0; x < ctx.width; ++x) {
                const float sx = sample_x + shift_vec_x;
                out_row[x] = sampler.Sample(sx);
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

        // General case - always process pixel by pixel
        float dist = dist0;
        float proj_len = dx0 * para_x + base_para;

        // Anti-aliasing constants
        const float feather = FEATHER_AMOUNT;
        const float neg_eff_plus_feather = -eff + feather;
        const float neg_eff_minus_feather = -eff - feather;
        const float feather_inv = 1.0f / (2.0f * feather);

        for (int x = 0; x < ctx.width; ++x) {
            // Pre-calculate border point
            const float border_x = anchor_x_f + proj_len * para_x;
            const float border_y = anchor_y_f + proj_len * para_y;

            if (dist > feather) {
                // Unchanged - sample from original position
                out_row[x] = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sample_x, sample_y, ctx.input_width, ctx.input_height);
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
                Pixel original_pixel = SampleBilinear<Pixel>(ctx.input_base, ctx.input_rowbytes, sample_x, sample_y, ctx.input_width, ctx.input_height);
                
                // coverage: 0 at feather (Original), 1 at -feather (Border)
                float t = (feather - dist) * feather_inv;
                out_row[x] = BlendPixels(original_pixel, border_pixel, t);
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

    // Null pointer checks
    if (!in_data || !params || !params[STRETCH_INPUT] || !output) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    // Checkout parameters for complex parameter access (especially ANCHOR_POINT)
    // This is required per Adobe SDK guidelines for accessing nested parameter data
    PF_ParamDef param;
    AEFX_CLR_STRUCT(param);

    // Checkout anchor point parameter to safely access its nested data
    PF_Err err = PF_CHECKOUT_PARAM(in_data,
                                   STRETCH_ANCHOR_POINT,
                                   in_data->current_time,
                                   in_data->time_step,
                                   in_data->time_scale,
                                   &param);
    if (err != PF_Err_NONE) {
        return err;
    }

    // Get anchor point values before checkin
    const int anchor_x = (param.u.td.x_value >> 16);
    const int anchor_y = (param.u.td.y_value >> 16);

    // Checkin the parameter immediately after extracting needed values
    err = PF_CHECKIN_PARAM(in_data, &param);
    if (err != PF_Err_NONE) {
        return err;
    }

    PF_EffectWorld* input = &params[STRETCH_INPUT]->u.ld;

    // Check data pointers
    if (!input->data || !output->data) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    const int width = output->width;
    const int height = output->height;
    const int input_width = input->width;
    const int input_height = input->height;

    if (width <= 0 || height <= 0) {
        return PF_Err_NONE;
    }

    // Maximum size validation to prevent memory issues
    constexpr int MAX_WIDTH = 16384;
    constexpr int MAX_HEIGHT = 16384;
    if (width > MAX_WIDTH || height > MAX_HEIGHT) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    const A_u_char* input_base = reinterpret_cast<const A_u_char*>(input->data);
    A_u_char* output_base = reinterpret_cast<A_u_char*>(output->data);
    const A_long input_rowbytes = input->rowbytes;
    const A_long output_rowbytes = output->rowbytes;

    // Parameters (anchor_x and anchor_y already extracted via PF_CHECKOUT_PARAM above)
    float angle_deg = static_cast<float>(params[STRETCH_ANGLE]->u.ad.value >> 16);
    const float angle_rad = angle_deg * (static_cast<float>(M_PI) / 180.0f);
    const float shift_amount = static_cast<float>(params[STRETCH_SHIFT_AMOUNT]->u.fs_d.value);
    const int direction = params[STRETCH_DIRECTION]->u.pd.value;

    // Downsample adjustment with division by zero protection (check both num > 0 and den != 0)
    const float downsample_x = (in_data->downsample_x.num > 0 && in_data->downsample_x.den != 0)
        ? static_cast<float>(in_data->downsample_x.den) / static_cast<float>(in_data->downsample_x.num)
        : 1.0f;
    const float downsample_y = (in_data->downsample_y.num > 0 && in_data->downsample_y.den != 0)
        ? static_cast<float>(in_data->downsample_y.den) / static_cast<float>(in_data->downsample_y.num)
        : 1.0f;
    const float downsample = std::min(downsample_x, downsample_y);

    // Effective shift in pixels with NaN/infinity validation
    float effective_shift = (downsample > 0.0f && std::isfinite(downsample))
        ? (shift_amount / downsample)
        : shift_amount;

    // Validate effective_shift is finite
    if (!std::isfinite(effective_shift)) {
        effective_shift = 0.0f;
    }

    if (std::abs(effective_shift) < 0.01f) {
        PF_Err copy_err = PF_COPY(input, output, nullptr, nullptr);
        if (copy_err != PF_Err_NONE) {
            return copy_err;
        }
        return PF_Err_NONE;
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

    // Parallel processing using std::thread
    // Safe because we only use our own SampleBilinear (no AE API calls)
    // Thread count limit to prevent excessive resource consumption
    constexpr int max_threads = 16;
    const int num_threads = std::min(max_threads, std::max(1, static_cast<int>(std::thread::hardware_concurrency())));
    const int rows_per_thread = (height + num_threads - 1) / num_threads;

    // Atomic error flag for proper error propagation from worker threads
    std::atomic<bool> has_error{false};

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    // Capture ctx by value to avoid dangling reference
    for (int t = 0; t < num_threads; ++t) {
        const int start_y = t * rows_per_thread;
        const int end_y = std::min(start_y + rows_per_thread, height);

        if (start_y >= height) break;

        threads.emplace_back([ctx, direction, start_y, end_y, &has_error]() {
            try {
                if (direction == 1) {
                    ProcessRowsBoth(ctx, start_y, end_y);
                }
                else if (direction == 2) {
                    ProcessRowsForward(ctx, start_y, end_y);
                }
                else {
                    ProcessRowsBackward(ctx, start_y, end_y);
                }
            }
            catch (const std::exception& e) {
                // Log error and set atomic flag
                (void)e; // Suppress unused warning in release builds
                has_error.store(true, std::memory_order_release);
            }
            catch (...) {
                // Catch any other exceptions and set error flag
                has_error.store(true, std::memory_order_release);
            }
        });
    }

    // Wait for all threads to complete
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // Check if any worker thread encountered an error
    if (has_error.load(std::memory_order_acquire)) {
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }

    return PF_Err_NONE;
}

static PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    (void)out_data;

    // Null pointer checks
    if (!in_data || !output) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    // Determine bit depth from world_flags
    // PF_WorldFlag_DEEP indicates 16-bit, absence indicates 8-bit
    // For 32-bit float, we check both input and output flags
    PF_EffectWorld* input = &params[STRETCH_INPUT]->u.ld;

    if ((output->world_flags & PF_WorldFlag_DEEP) ||
        (input->world_flags & PF_WorldFlag_DEEP)) {
        // 16-bit
        return RenderGeneric<PF_Pixel16>(in_data, out_data, params, output);
    }
    else {
        // Default to 8-bit
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
    (void)inSPBasicSuitePtr;
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

