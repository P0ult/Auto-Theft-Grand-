using UnrealBuildTool;

public class AutoTheftGrandEditorTarget : TargetRules
{
	public AutoTheftGrandEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("AutoTheftGrand");
	}
}
