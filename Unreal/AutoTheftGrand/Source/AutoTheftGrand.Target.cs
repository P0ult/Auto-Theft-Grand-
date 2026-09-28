using UnrealBuildTool;

public class AutoTheftGrandTarget : TargetRules
{
	public AutoTheftGrandTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("AutoTheftGrand");
	}
}
