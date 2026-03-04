/**************************************************************************/
/*  sandbox_profile.h                                                     */
/**************************************************************************/

#pragma once

#include "sandbox_config.h"
#include "sandbox_error.h"
#include "sandbox_limiter.h"

namespace hmsandbox {

// Sandbox profile: aggregates config, limiter, and error registry
// This is used by both HMSandbox (high-level) and GDScriptLanguage (VM-level)
struct SandboxProfile {
	HMSandboxConfig config;
	HMSandboxLimiter limiter;
	HMSandboxErrorRegistry errors;

	SandboxProfile() {}
};

} // namespace hmsandbox
