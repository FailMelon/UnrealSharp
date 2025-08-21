namespace UnrealSharpRoslynWeaver;

public class Program
{
    public static WeaverOptions WeaverOptions { get; private set; } = null!;

    public static int Main(string[] args)
    {
        WeaverOptions = WeaverOptions.ParseArguments(args);
        Console.Error.WriteLine("Not implemented yet.");
        return 1;
    }
}
