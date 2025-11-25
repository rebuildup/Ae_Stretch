#include "AEConfig.h"
#include "AE_EffectVers.h"
#define AE_STRETCH_PIPL_BUILD
#include "Stretch.h"
#undef AE_STRETCH_PIPL_BUILD

#ifndef AE_OS_WIN
	#include <AE_General.r>
#endif
	
resource 'PiPL' (16000) {
	{	
		Kind {
			AEEffect
		},
		
		Name {
			"Stretch"
		},
		
		Category {
			"361do plugins"
		},
#ifdef AE_OS_WIN
	#ifdef AE_PROC_INTELx64
		CodeWin64X86 {"EffectMain"},
	#endif
#else
	#ifdef AE_OS_MAC
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
	#endif
#endif
		
		AE_PiPL_Version {
			2,
			0
		},
		
	AE_Effect_Spec_Version {
		PF_PLUG_IN_VERSION,
		PF_PLUG_IN_SUBVERS
	},
	
	AE_Effect_Version {
		(MAJOR_VERSION << 19) +
		(MINOR_VERSION << 15) +
		(BUG_VERSION   << 11) +
		(STAGE_VERSION << 9)  +
		BUILD_VERSION
	},
	
	AE_Effect_Info_Flags {
		0
	},		AE_Effect_Global_OutFlags {
			0x02000400
		},
		
		AE_Effect_Global_OutFlags_2 {
			0x08000000
		},
		
		AE_Effect_Match_Name {
			"361do Stretch"
		},
		
		AE_Reserved_Info {
			0
		},
		
		AE_Effect_Support_URL {
			"https://github.com/rebuildup/Ae_Stretch"
		}
	}
};
