#include "Stretch.h"

typedef struct
{
	A_u_long index;
	A_char str[256];
} TableString;

TableString g_strs[StrID_NUMTYPES] = {
	StrID_NONE, "",
	StrID_Name, "Stretch",
	StrID_Description, "Stretches pixels based on an anchor point and angle"};

char *GetStringPtr(int strNum)
{
	return g_strs[strNum].str;
}