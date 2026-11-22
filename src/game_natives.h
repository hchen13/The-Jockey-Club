#pragma once
#include "native_diagnostics.h"

// main.h has already declared the actual imports. Instrument the unmodified
// SDK's invoke bridge locally, without patching the SDK or another ASI's calls.
#define nativeInit horse_diagnostics::Init
#define nativePush64 horse_diagnostics::Push
#define nativeCall horse_diagnostics::Call
#include <natives.h>
#undef nativeInit
#undef nativePush64
#undef nativeCall
