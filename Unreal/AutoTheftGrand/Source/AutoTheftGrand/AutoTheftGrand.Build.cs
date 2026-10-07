using UnrealBuildTool;

public class AutoTheftGrand : ModuleRules
{
	public AutoTheftGrand(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// (the simulation in Private/Sim is plain C++ and uses dynamic_cast)
		bUseRTTI = true;
		// The world generator (Private/Gen) is plain C++ shared with a command-line test harness, so it is
		// built without unity batching (its files use short helper names) and with relaxed shadow warnings.
		bUseUnity = false;
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Warning;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "ProceduralMeshComponent", "PhysicsCore",
			"MeshDescription", "StaticMeshDescription", "RenderCore", "RHI"
		});
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "AudioExtensions" });
		if (Target.bBuildEditor)
		{
			// the materials are generated and saved as assets from C++ when the editor starts
			PrivateDependencyModuleNames.AddRange(new string[] { "AssetRegistry" });
		}
		PrivateIncludePaths.Add(ModuleDirectory + "/Private");
		PrivateIncludePaths.Add(ModuleDirectory + "/Private/Gen"); // (the simulation includes the generator by plain names)
	}
}
