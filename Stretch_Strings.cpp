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
	// Bounds checking to prevent out-of-bounds access
	if (strNum >= 0 && strNum < StrID_NUMTYPES) {
		return g_strs[strNum].str;
	}
	// Return empty string for invalid indices
	static char empty_str[] = "";
	return empty_str;
}