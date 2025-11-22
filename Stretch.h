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
    ANCHOR_POINT_ID = 1,
    ANGLE_ID,
    SHIFT_AMOUNT_ID,
    DIRECTION_ID,
    STRETCH_NUM_PARAMS
};
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
    ANCHOR_POINT_ID = 1,
    ANGLE_ID,
    SHIFT_AMOUNT_ID,
    DIRECTION_ID,
    STRETCH_NUM_PARAMS
};

#ifdef __cplusplus
extern "C"
{
#endif

    DllExport PF_Err
    EffectMain(
        PF_Cmd cmd,
        PF_ParamDef *params[],
        PF_LayerDef *output,
        void *extra);

#ifdef __cplusplus
}
#endif

#endif // STRETCH_H
