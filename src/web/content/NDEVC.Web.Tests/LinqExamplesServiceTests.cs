using Microsoft.EntityFrameworkCore;
using NDEVC.Web.Api.LinqDemo;

namespace NDEVC.Web.Tests;

public class LinqExamplesServiceTests
{
    [Fact]
    public async Task GetExamplesAsync_ReturnsAllRequestedOperators()
    {
        var dbPath = Path.Combine(Path.GetTempPath(), $"linq-demo-test-{Guid.NewGuid():N}.db");
        try
        {
            var options = new DbContextOptionsBuilder<LinqDemoDbContext>()
                .UseSqlite($"Data Source={dbPath}")
                .Options;

            await using var db = new LinqDemoDbContext(options);
            await LinqDemoSeeder.ResetAsync(db);

            var service = new LinqExamplesService(db);
            var response = await service.GetExamplesAsync("alice@example.com", 1, 2);

            Assert.Equal(19, response.Examples.Count);
            Assert.Contains(response.Examples, x => x.Operator == "Where");
            Assert.Contains(response.Examples, x => x.Operator == "Select");
            Assert.Contains(response.Examples, x => x.Operator == "SelectMany");
            Assert.Contains(response.Examples, x => x.Operator == "OrderBy");
            Assert.Contains(response.Examples, x => x.Operator == "OrderByDescending");
            Assert.Contains(response.Examples, x => x.Operator == "ThenBy");
            Assert.Contains(response.Examples, x => x.Operator == "GroupBy");
            Assert.Contains(response.Examples, x => x.Operator == "Join");
            Assert.Contains(response.Examples, x => x.Operator == "Any");
            Assert.Contains(response.Examples, x => x.Operator == "All");
            Assert.Contains(response.Examples, x => x.Operator == "FirstOrDefault");
            Assert.Contains(response.Examples, x => x.Operator == "SingleOrDefault");
            Assert.Contains(response.Examples, x => x.Operator == "Count");
            Assert.Contains(response.Examples, x => x.Operator == "Skip");
            Assert.Contains(response.Examples, x => x.Operator == "Take");
            Assert.Contains(response.Examples, x => x.Operator == "Sum");
            Assert.Contains(response.Examples, x => x.Operator == "Min/Max");
            Assert.Contains(response.Examples, x => x.Operator == "Include");
            Assert.Contains(response.Examples, x => x.Operator == "ToList");
        }
        finally
        {
            try
            {
                if (File.Exists(dbPath))
                {
                    File.Delete(dbPath);
                }
            }
            catch (IOException)
            {
                // SQLite can keep a short-lived handle after context disposal on some runners.
            }
        }
    }
}
