// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class msc_vr : ModuleRules
{
	public msc_vr(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"msc_vr",
			"msc_vr/Variant_Horror",
			"msc_vr/Variant_Horror/UI",
			"msc_vr/Variant_Shooter",
			"msc_vr/Variant_Shooter/AI",
			"msc_vr/Variant_Shooter/UI",
			"msc_vr/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
