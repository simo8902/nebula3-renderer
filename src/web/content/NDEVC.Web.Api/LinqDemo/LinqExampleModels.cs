namespace NDEVC.Web.Api.LinqDemo;

public sealed class UserDto
{
    public int Id { get; set; }
    public string Name { get; set; } = string.Empty;
}

public sealed record LinqExampleResult(
    string Operator,
    string Purpose,
    string Query,
    object? Result);

public sealed record LinqExamplesResponse(
    DateTime GeneratedUtc,
    string DatabasePath,
    string EmailFilter,
    int Page,
    int Size,
    IReadOnlyList<LinqExampleResult> Examples);
