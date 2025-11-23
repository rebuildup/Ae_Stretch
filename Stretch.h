#pragma once
#ifndef STRETCH_H
#define STRETCH_H

#ifdef _WIN32
#define _USE_MATH_DEFINES
#define NOMINMAX
#endif

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEGP_SuiteHandler.h"
#include <cmath>

#ifdef _WIN32
#include <Windows.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#endif

// Define Smart Render structs locally to avoid missing header issues
typedef struct PF_SmartRenderCallbacks_Local {
    PF_Err (*checkout_layer_pixels)(PF_ProgPtr effect_ref, PF_ParamIndex index, PF_EffectWorld **pixels);
    PF_Err (*checkout_output)(PF_ProgPtr effect_ref, PF_EffectWorld **output);
    PF_Err (*checkin_layer_pixels)(PF_ProgPtr effect_ref, PF_ParamIndex index);
    PF_Err (*is_layer_pixel_data_valid)(PF_ProgPtr effect_ref, PF_ParamIndex index, PF_Boolean *valid);
} PF_SmartRenderCallbacks_Local;

typedef struct PF_SmartRenderExtra_Local {
    PF_SmartRenderCallbacks_Local *cb;
    void *unused;
} PF_SmartRenderExtra_Local;

typedef struct PF_RenderRequest_Local {
    PF_LRect rect;
    PF_Field field;
    PF_ChannelMatrix channel_matrix;
    PF_Boolean preserve_rgb_of_zero_alpha;
    PF_Boolean preserve_rgb_of_zero_alpha_is_valid;
    void *unused;
} PF_RenderRequest_Local;

typedef struct PF_CheckoutResult_Local {
    PF_LRect result_rect;
    PF_LRect max_result_rect;
    PF_Boolean par_varying;
    PF_Boolean par_varying_is_valid;
    void *unused;
} PF_CheckoutResult_Local;

typedef struct PF_PreRenderInput_Local {
    PF_RenderRequest_Local output_request;
    short bit_depth;
    void *unused;
} PF_PreRenderInput_Local;

typedef struct PF_PreRenderOutput_Local {
    PF_LRect result_rect;
    PF_LRect max_result_rect;
    void *unused;
} PF_PreRenderOutput_Local;

typedef struct PF_PreRenderCallbacks_Local {
    PF_Err (*checkout_layer)(PF_ProgPtr effect_ref, PF_ParamIndex index, PF_ParamIndex req_index, const PF_RenderRequest_Local *req, A_long current_time, A_long time_step, A_u_long time_scale, PF_CheckoutResult_Local *result);
    PF_Err (*checkout_layer_pixels)(PF_ProgPtr effect_ref, PF_ParamIndex index, PF_ParamIndex req_index, const PF_RenderRequest_Local *req, A_long current_time, A_long time_step, A_u_long time_scale, PF_EffectWorld **pixels);
} PF_PreRenderCallbacks_Local;

typedef struct PF_PreRenderExtra_Local {
    PF_PreRenderCallbacks_Local *cb;
    PF_PreRenderInput_Local *input;
    PF_PreRenderOutput_Local *output;
    void *unused;
} PF_PreRenderExtra_Local;

#ifndef DllExport
#if defined(_WIN32)
#define DllExport __declspec(dllexport)
#else
#define DllExport
#endif
#endif

#define MAJOR_VERSION 1
#define MINOR_VERSION 1
#define BUG_VERSION 0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 1
#define NAME "stretch_v2"
#define DESCRIPTION "Stretches pixels based on an anchor point and angle"

// String IDs
enum
{
    StrID_NONE = 0,
    StrID_Name,
    StrID_Description,
    StrID_NUMTYPES
};

enum
{
    STRETCH_INPUT = 0,
    STRETCH_TYPE,
    STRETCH_VALUE,
    STRETCH_DIRECTION,
    STRETCH_INTERPOLATION,
    STRETCH_NUM_PARAMS
};

#ifdef __cplusplus
extern "C"
{
#endif

    DllExport PF_Err
    PluginDataEntryFunction2(
        PF_PluginDataPtr inPtr,
        PF_PluginDataCB2 inPluginDataCallBackPtr,
        SPBasicSuite *inSPBasicSuitePtr,
        const char *inHostName,
        const char *inHostVersion);

    DllExport PF_Err
    EffectMain(
        PF_Cmd cmd,
        PF_InData *in_data,
        PF_OutData *out_data,
        PF_ParamDef *params[],
        PF_LayerDef *output,
        void *extra);

#ifdef __cplusplus
}
#endif

#endif // STRETCH_H
