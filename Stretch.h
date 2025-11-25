#pragma once
#ifndef STRETCH_H
#define STRETCH_H

#include "AEConfig.h"

#define MAJOR_VERSION 1
#define MINOR_VERSION 2
#define BUG_VERSION 0
#define BUILD_VERSION 1
#define NAME "Stretch"
#define DESCRIPTION "Stretches pixels based on an anchor point and angle"

#ifdef AE_STRETCH_PIPL_BUILD

#ifndef PF_Stage_DEVELOP
#define PF_Stage_DEVELOP 0
#endif
#define STAGE_VERSION 0

#ifndef PF_Vers_VERS_BITS
#define PF_Vers_VERS_BITS 0x7L
#define PF_Vers_VERS_SHIFT 19
#define PF_Vers_VERS_HIGH_BITS 0xfL
#define PF_Vers_VERS_HIGH_SHIFT 26
#define PF_Vers_VERS_LOW_SHIFT 3
#define PF_Vers_SUBVERS_BITS 0xfL
#define PF_Vers_SUBVERS_SHIFT 15
#define PF_Vers_BUGFIX_BITS 0xfL
#define PF_Vers_BUGFIX_SHIFT 11
#define PF_Vers_STAGE_BITS 0x3L
#define PF_Vers_STAGE_SHIFT 9
#define PF_Vers_BUILD_BITS 0x1ffL
#define PF_Vers_BUILD_SHIFT 0
#endif

#define STRETCH_VERSION                                                                                    \
    (((((MAJOR_VERSION >> PF_Vers_VERS_LOW_SHIFT) & PF_Vers_VERS_HIGH_BITS) << PF_Vers_VERS_HIGH_SHIFT)) | \
     ((((MAJOR_VERSION) & PF_Vers_VERS_BITS) << PF_Vers_VERS_SHIFT)) |                                     \
     ((((MINOR_VERSION) & PF_Vers_SUBVERS_BITS) << PF_Vers_SUBVERS_SHIFT)) |                               \
     ((((BUG_VERSION) & PF_Vers_BUGFIX_BITS) << PF_Vers_BUGFIX_SHIFT)) |                                   \
     ((((STAGE_VERSION) & PF_Vers_STAGE_BITS) << PF_Vers_STAGE_SHIFT)) |                                   \
     ((((BUILD_VERSION) & PF_Vers_BUILD_BITS) << PF_Vers_BUILD_SHIFT)))

#else

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

#endif // AE_STRETCH_PIPL_BUILD

#endif // STRETCH_H
