using System.Collections.ObjectModel;

namespace UnrealSharpBuildTool.Actions;

public class PackageProject : BuildToolAction
{
    public override bool RunAction()
    {
        string archiveDirectoryPath = Program.TryGetArgument("ArchiveDirectory");

        if (string.IsNullOrEmpty(archiveDirectoryPath))
        {
            throw new Exception("ArchiveDirectory argument is required for the Publish action.");
        }

        string rootProjectPath = Path.Combine(archiveDirectoryPath, Program.BuildToolOptions.ProjectName);
        string binariesPath = Program.GetOutputPath(rootProjectPath);
        string bindingsPath = Path.Combine(Program.BuildToolOptions.PluginDirectory, "Managed", "UnrealSharp");
        string bindingsOutputPath = Path.Combine(Program.BuildToolOptions.PluginDirectory, "Intermediate", "Build", "Managed");

        Collection<string> extraArguments =
        [
            "--self-contained",
            "--runtime",
            "win-x64",
            "-p:DisableWithEditor=true",
            $"-p:PublishDir=\"{binariesPath}\"",
            $"-p:OutputPath=\"{bindingsOutputPath}\"",
        ];

        BuildSolution buildBindings = new BuildSolution(bindingsPath, extraArguments, BuildConfig.Publish);
        buildBindings.RunAction();

        bool.TryParse(Program.TryGetArgument("UseRoslyn"), out bool useRoslyn);
        if (useRoslyn)
        {
            RoslynWeaverProject roslynWeaveProject = new RoslynWeaverProject(binariesPath, BuildConfig.Publish);
            roslynWeaveProject.RunAction();
        }
        else
        {
            BuildUserSolution buildUserSolution = new BuildUserSolution(null, BuildConfig.Publish);
            buildUserSolution.RunAction();

            WeaveProject weaveProject = new WeaveProject(binariesPath);
            weaveProject.RunAction();
        }
        
        return true;
    }
}
