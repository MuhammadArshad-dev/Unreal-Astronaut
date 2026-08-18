// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class Astronaut : ModuleRules
{
	public Astronaut(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore",
			"AudioCaptureCore",
			"WebSockets",
			"Json",
			"JsonUtilities",
			"SpeechAnimationSolver",
			"LiveLinkInterface",
			"LiveLink",
			"LiveLinkAnimationCore",
			"NNE",
			"AudioPlatformConfiguration"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "ThirdParty"));

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
