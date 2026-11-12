using System;
using System.IO;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;

// Builds an empty iOS player shell (and a matching macOS player for data-layout diffing).
// The shell's engine is later paired with the Steam build's GameAssembly + Data.
public static class BuildShell
{
    const string BuildRoot = "/private/tmp/temp/manosaba-ipad/build";
    const string ScenePath = "Assets/Shell.unity";

    static void EnsureScene()
    {
        if (File.Exists(ScenePath)) return;
        var scene = EditorSceneManager.NewScene(NewSceneSetup.DefaultGameObjects, NewSceneMode.Single);
        EditorSceneManager.SaveScene(scene, ScenePath);
        Debug.Log("[BuildShell] created " + ScenePath);
    }

    static void ConfigureCommon()
    {
        PlayerSettings.companyName = "Re,AER";
        PlayerSettings.productName = "manosaba";
        PlayerSettings.stripEngineCode = false;
        PlayerSettings.colorSpace = ColorSpace.Linear;
        PlayerSettings.defaultInterfaceOrientation = UIOrientation.AutoRotation;
        PlayerSettings.allowedAutorotateToPortrait = false;
        PlayerSettings.allowedAutorotateToPortraitUpsideDown = false;
        PlayerSettings.allowedAutorotateToLandscapeLeft = true;
        PlayerSettings.allowedAutorotateToLandscapeRight = true;
    }

    static void ConfigureIOS()
    {
        var t = NamedBuildTarget.iOS;
        // Your own bundle id and team: MANOSABA_BUNDLE_ID / DEVELOPMENT_TEAM (signing can also be set in Xcode).
        PlayerSettings.SetApplicationIdentifier(t, System.Environment.GetEnvironmentVariable("MANOSABA_BUNDLE_ID") ?? "com.example.manosaba");
        PlayerSettings.SetScriptingBackend(t, ScriptingImplementation.IL2CPP);
        PlayerSettings.SetManagedStrippingLevel(t, ManagedStrippingLevel.Minimal);
        PlayerSettings.SetArchitecture(t, 1); // ARM64
        PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.iOS, false);
        PlayerSettings.SetGraphicsAPIs(BuildTarget.iOS, new[] { GraphicsDeviceType.Metal });
        PlayerSettings.iOS.targetDevice = iOSTargetDevice.iPhoneAndiPad;
        PlayerSettings.iOS.targetOSVersionString = "15.0";
        PlayerSettings.iOS.appleDeveloperTeamID = System.Environment.GetEnvironmentVariable("DEVELOPMENT_TEAM") ?? "";
        PlayerSettings.iOS.appleEnableAutomaticSigning = true;
        PlayerSettings.iOS.requiresFullScreen = true;
        PlayerSettings.statusBarHidden = true;
    }

    static void Build(BuildTarget target, string outPath)
    {
        EnsureScene();
        ConfigureCommon();
        if (target == BuildTarget.iOS) ConfigureIOS();
        var opts = new BuildPlayerOptions
        {
            scenes = new[] { ScenePath },
            target = target,
            locationPathName = outPath,
            options = BuildOptions.None,
        };
        Debug.Log($"[BuildShell] building {target} -> {outPath}");
        BuildReport report = BuildPipeline.BuildPlayer(opts);
        Debug.Log($"[BuildShell] result={report.summary.result} errors={report.summary.totalErrors} size={report.summary.totalSize}");
        if (report.summary.result != BuildResult.Succeeded)
            EditorApplication.Exit(1);
    }

    public static void IOS()
    {
        PlayerSettings.iOS.sdkVersion = iOSSdkVersion.DeviceSDK;
        Build(BuildTarget.iOS, BuildRoot + "/xcode");
    }

    // Apple-silicon simulator variant: same shell, links libiPhone-lib-sim-arm64.dylib.
    public static void IOSSim()
    {
        PlayerSettings.iOS.sdkVersion = iOSSdkVersion.SimulatorSDK;
        PlayerSettings.iOS.simulatorSdkArchitecture = AppleMobileArchitectureSimulator.ARM64;
        try { Build(BuildTarget.iOS, BuildRoot + "/xcode-sim"); }
        finally { PlayerSettings.iOS.sdkVersion = iOSSdkVersion.DeviceSDK; }
    }

    public static void Mac()
    {
        PlayerSettings.SetScriptingBackend(NamedBuildTarget.Standalone, ScriptingImplementation.Mono2x);
        Build(BuildTarget.StandaloneOSX, BuildRoot + "/mac/Shell.app");
    }
}
