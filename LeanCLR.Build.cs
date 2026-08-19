using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class LeanCLR : ModuleRules
{
	public LeanCLR(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		const int MajorVersion = 10;

		const int MinorVersion = 0;

		const int PatchVersion = 4;

		PublicDefinitions.AddRange(new string[]
		{
			$"DOTNET_MAJOR_VERSION={MajorVersion}",
			$"DOTNET_MINOR_VERSION={MinorVersion}",
			$"DOTNET_PATCH_VERSION={PatchVersion}"
		});

		var bUseRelease = true;

		var bIsDebug = !bUseRelease &&
		               (Target.Configuration == UnrealTargetConfiguration.Debug ||
		                Target.Configuration == UnrealTargetConfiguration.DebugGame);

		var LeanCLRConfiguration = bIsDebug ? "Debug" : "Release";

		PublicDefinitions.Add($"LEANCLR_CONFIGURATION=TEXT(\"{LeanCLRConfiguration}\")");

		PublicDefinitions.Add("LEANCLR_GC_MARK_SWEEP=1");

		PublicDefinitions.Add("LEANCLR_GC_DEBUG=1");

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "src", "runtime"));

		var LibraryPath = Path.Combine(ModuleDirectory, "lib", LeanCLRConfiguration);

		var NetLibraryPath = Path.Combine(LibraryPath, "net");

		foreach (var File in GetFiles(NetLibraryPath))
		{
			RuntimeDependencies.Add(
				$"$(BinaryOutputDir)/LeanCLR/{Target.Platform}/net/{Path.GetFileName(File)}", File);
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			var PlatformLibraryPath = Path.Combine(LibraryPath, Target.Platform.ToString());

			PublicAdditionalLibraries.Add(Path.Combine(PlatformLibraryPath,
				"leanclr.lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			var PlatformLibraryPath = Path.Combine(LibraryPath, "Linux_x86_64");

			PublicAdditionalLibraries.Add(Path.Combine(PlatformLibraryPath,
				"libleanclr.a"));

			PublicSystemLibraries.Add("dl");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			var PlatformLibraryPath = Path.Combine(LibraryPath,
#if UE_5_2_OR_LATER
				Target.Architecture.bIsX64
#else
				Target.Architecture == "x86_64" || Target.Architecture == "x64"
#endif

					? "macOS_x86_64"
					: "macOS_arm64");

			PublicAdditionalLibraries.Add(Path.Combine(PlatformLibraryPath,
				"libleanclr.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			var PlatformLibraryPath = Path.Combine(LibraryPath, Target.Platform.ToString());

			PublicAdditionalLibraries.Add(Path.Combine(PlatformLibraryPath,
				"libleanclr.a"));

			PublicSystemLibraries.Add("dl");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			var PlatformLibraryPath = Path.Combine(LibraryPath, Target.Platform.ToString());

			PublicAdditionalLibraries.Add(Path.Combine(PlatformLibraryPath,
				"libleanclr.a"));
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