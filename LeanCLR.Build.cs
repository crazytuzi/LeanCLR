using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

// LeanCLR third script backend (from-scratch IL interpreter + GC).
// Modeled on Mono.Build.cs / CoreCLR.Build.cs: ModuleType.External with per-platform dispatch.
//
// P2 scope: Win64 only is filled in; every other platform keeps a placeholder branch + TODO so
// adding a platform later is an incremental fill (deliver that platform's leanclr static lib +
// corlib) rather than a refactor. Per design §4.8, ALL platform-specific differences must stay in
// exactly two places: this file (lib / runtime deps / system libs) and FLeanCLRFunctionLibrary
// (corlib / publish / search directories). FLeanCLRDomain.cpp must contain no platform branches.
public class LeanCLR : ModuleRules
{
	public LeanCLR(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		// ------------------------------------------------------------------
		// Self-contained vendored layout (P7.1 — resolves the old TODO(P2.2)/R4 hardcoded LeanCLRRoot).
		// leanclr headers + per-platform static libs now live in-repo under this ThirdParty module, so
		// any machine/CI can build without a leanclr checkout or an env var:
		//   src/runtime/                      -> PublicSystemIncludePaths (headers only; .cpp are in the lib)
		//   lib/<Platform>/<Config>/<libfile> -> PublicAdditionalLibraries (regularized layout, design §4.8.2)
		// Static libs are produced externally and copied into lib/<Platform>/<Config>/ (P7.2); the .NET
		// BCL (corlib) is vendored the same self-contained way under lib/<Platform>/<Config>/net (P7.3).
		// ------------------------------------------------------------------
		var bUseRelease = true;

		var bIsDebug = !bUseRelease &&
		               (Target.Configuration == UnrealTargetConfiguration.Debug ||
		                Target.Configuration == UnrealTargetConfiguration.DebugGame);

		var LeanCLRConfiguration = bIsDebug ? "Debug" : "Release";

		PublicDefinitions.Add($"LEANCLR_CONFIGURATION=TEXT(\"{LeanCLRConfiguration}\")");

		// .NET version macros consumed by the shared CrossVersion/ScriptCodeGenerator code
		// (DotnetVersion.h, FSolutionGenerator.cpp). Each backend module publishes these; because the
		// three backends are mutually exclusive, LeanCLR must publish them too or those consumers see
		// undeclared identifiers when LeanCLR is the active backend. LeanCLR vendors its own copy of the
		// same .NET 10 BCL (under lib/<Platform>/<Config>/net), so the version matches CoreCLR.Build.cs.
		const int MajorVersion = 10;

		const int MinorVersion = 0;

		const int PatchVersion = 4;

		PublicDefinitions.AddRange(new string[]
		{
			$"DOTNET_MAJOR_VERSION={MajorVersion}",
			$"DOTNET_MINOR_VERSION={MinorVersion}",
			$"DOTNET_PATCH_VERSION={PatchVersion}"
		});

		// ------------------------------------------------------------------
		// Cross-platform (outside any platform branch, per design §4.1 / §4.8.3):
		//   - include root src/runtime is identical on every platform
		//     (#include "vm/runtime.h", "public/leanclr.h", ...)
		//   - GC algorithm macros MUST match the compiled lib (see runtime/CMakeLists.txt):
		//     LEANCLR_GC_MARK_SWEEP=1, and LEANCLR_GC_DEBUG=1 as the current lib was built with it.
		// ------------------------------------------------------------------
		// Use PublicSystemIncludePaths (not PublicIncludePaths) so leanclr's headers are treated as
		// external/system headers: UE compiles with warnings-as-errors and a strict warning level,
		// and leanclr's from-scratch headers are not warning-clean (e.g. C4458 in cli_image.h). System
		// include paths get /external:I + /external:W0 on MSVC, silencing those without touching leanclr.
		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "src", "runtime"));

		PublicDefinitions.Add("LEANCLR_GC_MARK_SWEEP=1");
		PublicDefinitions.Add("LEANCLR_GC_DEBUG=1");

		// Regularized library layout is lib/<Platform>/<Config>/<libfile> (design §4.8.2), with the code
		// deriving <Platform> from Target.Platform.ToString(). Each platform's static lib is produced
		// externally (P7.2) and vendored under this ThirdParty module. Note <Platform> cannot be folded
		// away there: Linux/Android/Mac/iOS all name their archive libleanclr.a.
		//
		// The corlib, by contrast, is not platform-specific, so it is vendored once per configuration at
		// lib/net/<Config> (P7.3 + §13.5 C1) instead of once per (platform, config): leanclr is an IL
		// interpreter, and .NET assemblies (.dll) hold only metadata + IL bytecode with no CPU-native
		// instructions, so the same BCL assemblies work identically on every platform. The <Config> split
		// is kept because CoreCLR's vendored Debug and Release BCLs genuinely differ (unoptimized vs
		// optimized assemblies — Debug's System.Private.CoreLib.dll is 23.5MB vs Release's 16.0MB); both
		// are copied straight from ThirdParty/CoreCLR/lib/<Config>/Win64/net.
		//
		// It is the full CoreCLR win-x64 BCL on purpose, on every platform: the only thing a corlib
		// "flavor" decides is which native library it P/Invokes into, and leanclr emulates the Win32
		// surface (Interop/Kernel32::* + a small Interop/Sys subset) — it implements none of the ~157
		// SystemNative_* entrypoints a Unix-flavor corlib needs, and its LoadICU() deliberately returns 0
		// so the Windows flavor takes its NLS path (P7 §12.3/§12.4). Mono's Android/iOS BCL and CoreCLR's
		// Unix BCL both fail hard here; do not "fix" this by reaching for the target platform's own net
		// dir. (Both vendored configs are Windows flavor — verified: SystemNative_*=0, kernel32 present,
		// System.Globalization.UseNls present, no ICU FailFast string.)
		var BclSourceDir = Path.Combine(ModuleDirectory, "lib", "net", LeanCLRConfiguration);

		// Staged next to the binaries as LeanCLR/<Platform>/net to match the runtime contract in
		// FLeanCLRFunctionLibrary::GetCorlibDirectory() (which derives <Platform> from
		// FPlatformProcess::GetBinariesSubdirectory()). Called from each supported platform branch below
		// rather than unconditionally, so an unfilled platform stages nothing.
		void StageCorlib()
		{
			foreach (var File in GetFiles(BclSourceDir))
			{
				RuntimeDependencies.Add(
					$"$(BinaryOutputDir)/LeanCLR/{Target.Platform}/net/{Path.GetFileName(File)}", File);
			}
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib",
				Target.Platform.ToString(), LeanCLRConfiguration, "leanclr.lib"));

			StageCorlib();
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib",
				Target.Platform.ToString(), LeanCLRConfiguration, "libleanclr.a"));

			// dlopen/dlsym/dlclose (POSIX branch of Kernel32::load_library_ex, added in leanclr b404e76).
			// UE's Linux link line already carries -lpthread/-lm/-lrt, but the module declaring its own
			// dependencies keeps it correct if that ever changes.
			PublicSystemLibraries.Add("dl");

			StageCorlib();
		}
		else if (Target.Platform == UnrealTargetPlatform.LinuxArm64)
		{
			// TODO(TP-Linux/aarch64): deliver an aarch64 libleanclr.a, then mirror the Linux branch above.
			// The vendored lib/Linux lib is ELF64 x86-64 only, so LinuxArm64 must stay unfilled rather
			// than share that branch (it would link the wrong architecture).
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// Mac has two archs under one UnrealTargetPlatform, so <Platform> is not Target.Platform.ToString()
			// here; it is the arch dir, matching Mono/CoreCLR (macOS_x86_64 / macOS_arm64). Only arm64 is
			// vendored today; the x86_64 dir stays unfilled until an Intel libleanclr.a is delivered.
			var MacArch = Target.Architecture.bIsX64 ? "macOS_x86_64" : "macOS_arm64";

			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib",
				MacArch, LeanCLRConfiguration, "libleanclr.a"));

			// No PublicSystemLibraries("dl"): on macOS dlopen/dlsym/dlclose live in libSystem (linked by
			// default), unlike Linux which needs -ldl. corlib needs no per-Mac copy — StageCorlib sources
			// the single vendored BCL and stages it under LeanCLR/Mac/net (Target.Platform.ToString()).
			StageCorlib();
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib",
				Target.Platform.ToString(), LeanCLRConfiguration, "libleanclr.a"));

			PublicSystemLibraries.Add("dl");

			StageCorlib();
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			// iOS is arm64-only, so <Platform> is Target.Platform.ToString() ("IOS") — no arch split like Mac.
			// Pure interpreter, zero executable-memory generation: no JIT, so iOS's W^X ban is a non-issue
			// (LeanCLR's key mobile advantage over Mono/CoreCLR — no full AOT pipeline needed).
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib",
				Target.Platform.ToString(), LeanCLRConfiguration, "libleanclr.a"));

			// No PublicSystemLibraries("dl"): on iOS dlopen/dlsym/dlclose live in libSystem (linked by
			// default), same as macOS. corlib needs no per-iOS copy — StageCorlib sources the single
			// vendored win-x64 BCL and stages it under LeanCLR/IOS/net (Target.Platform.ToString()).
			StageCorlib();
		}
	}

	private static IEnumerable<string> GetFiles(string InDirectory, string InPattern = "*.*")
	{
		var Files = new List<string>();

		foreach (var File in Directory.GetFiles(InDirectory, InPattern))
		{
			Files.Add(File);
		}

		foreach (var File in Directory.GetDirectories(InDirectory))
		{
			Files.AddRange(GetFiles(File, InPattern));
		}

		return Files;
	}
}
