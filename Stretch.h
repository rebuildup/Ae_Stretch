#pragma once
#ifndef STRETCH_H
#define STRETCH_H

#define MAJOR_VERSION 1
#define MINOR_VERSION 2
#define BUG_VERSION 0
#define BUILD_VERSION 0
#define NAME "Stretch"
#define DESCRIPTION "Stretches pixels based on an anchor point and angle"

#ifdef AE_STRETCH_PIPL_BUILD

#ifndef PF_Stage_DEVELOP
#define PF_Stage_DEVELOP 0
#endif
#define STAGE_VERSION 0

#define STRETCH_VERSION PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION)

#else

#include "AEConfig.h"

#ifdef _WIN32
#define _USE_MATH_DEFINES
#define NOMINMAX
#endif

#include "AE_Effect.h"
#define STAGE_VERSION PF_Stage_DEVELOP
#define STRETCH_VERSION PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION)

#include "entry.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEGP_SuiteHandler.h"
#include <cmath>

#if defined(_WIN32)
#include <Windows.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#endif

#ifndef DllExport
#if defined(_WIN32)
#define DllExport __declspec(dllexport)
#else
#define DllExport
#endif
#endif

// String IDs
typedef enum {
    StrID_NONE = 0,
    StrID_Name,
    StrID_Description,
    StrID_NUMTYPES
} StrIDType;

enum
{
    STRETCH_INPUT = 0,
    STRETCH_SHIFT_AMOUNT,
    STRETCH_ANCHOR_POINT,
    STRETCH_ANGLE,
    STRETCH_DIRECTION,
    STRETCH_NUM_PARAMS
};

enum
{
    SHIFT_AMOUNT_DISK_ID = 1,
    ANCHOR_POINT_DISK_ID,
    ANGLE_DISK_ID,
    DIRECTION_DISK_ID
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

// Constants for anti-aliasing and sampling
constexpr float ALPHA_THRESHOLD = 0.001f;
constexpr float FEATHER_AMOUNT = 0.5f;
constexpr float EPSILON = 0.001f;
constexpr float WEIGHT_THRESHOLD = 0.999f;

// Floating point comparison helper
constexpr inline bool IsApproximatelyEqual(float a, float b, float epsilon = EPSILON) {
    return (a > b ? a - b : b - a) < epsilon;
}

#endif // AE_STRETCH_PIPL_BUILD

#endif // STRETCH_H
