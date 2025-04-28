#include "Skeleton.h"

typedef struct {
	A_u_long	index;
	A_char		str[256];
} TableString;

TableString		g_strs[StrID_NUMTYPES] = {
	StrID_NONE,						"",
	StrID_Name,						"stretch",
	StrID_Description,				"hello !"
};

char* GetStringPtr(int strNum)
{
	return g_strs[strNum].str;
}