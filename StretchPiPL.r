#include "AEConfig.h"
#include "AE_EffectVers.h"

/* Include AE_General.r for resource definitions on Mac */
#ifdef AE_OS_MAC
	#include <AE_General.r>
#endif

#if defined(__MACH__) && !defined(AE_OS_MAC)
	#define AE_OS_MAC 1
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
			"361do_plugins"
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
		589825    /* 1.2.0 */
	},
	
	AE_Effect_Info_Flags {
		0
	},
	
	AE_Effect_Global_OutFlags {
		// PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_PIX_INDEPENDENT | PF_OutFlag_I_EXPAND_BUFFER
		0x02000600
	},
		
		AE_Effect_Global_OutFlags_2 {
			// PF_OutFlag2_SUPPORTS_THREADED_RENDERING
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
