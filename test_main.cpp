#include "Stretch.h"
#include <iostream>

// Forward declaration of Render so we can call it.
static PF_Err Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output);

int main() {
    PF_InData in_data{};
    PF_OutData out_data{};

    PF_ParamDef param_input{};
    PF_ParamDef param_anchor{};
    PF_ParamDef param_angle{};
    PF_ParamDef param_shift{};
    PF_ParamDef param_direction{};

    PF_ParamDef* params[STRETCH_NUM_PARAMS]{};

    // Simulate a small 64x64 8bpc layer
    PF_Pixel pixels_in[64 * 64]{};
    PF_Pixel pixels_out[64 * 64]{};

    PF_EffectWorld input{};
    PF_EffectWorld output{};

    input.width = 64;
    input.height = 64;
    input.rowbytes = 64 * sizeof(PF_Pixel);
    input.data = pixels_in;

    output.width = 64;
    output.height = 64;
    output.rowbytes = 64 * sizeof(PF_Pixel);
    output.data = pixels_out;

    param_input.u.ld = input;

    // Anchor at center
    param_anchor.u.td.x_value = 32 << 16;
    param_anchor.u.td.y_value = 32 << 16;

    // Angle 45 degrees
    param_angle.u.ad.value = 45 << 16;

    // Shift amount
    param_shift.u.fs_d.value = 50.0;

    // Direction both
    param_direction.u.pd.value = 1;

    params[0] = &param_input;
    params[ANCHOR_POINT_ID] = &param_anchor;
    params[ANGLE_ID] = &param_angle;
    params[SHIFT_AMOUNT_ID] = &param_shift;
    params[DIRECTION_ID] = &param_direction;

    in_data.downsample_x.num = 1;
    in_data.downsample_x.den = 1;
    in_data.downsample_y.num = 1;
    in_data.downsample_y.den = 1;

    // world_flags = 0 -> 8bpc
    output.world_flags = 0;

    PF_Err err = Render(&in_data, &out_data, params, &output);

    std::cout << "Render err=" << err << " first pixel a=" << (int)pixels_out[0].alpha << "\n";
    return 0;
}
