using Microsoft.EntityFrameworkCore;

namespace NDEVC.Web.Api.LinqDemo;

public sealed class LinqExamplesService(LinqDemoDbContext db)
{
    public async Task<LinqExamplesResponse> GetExamplesAsync(
        string? email,
        int? page,
        int? size,
        CancellationToken ct = default)
    {
        var effectiveEmail = string.IsNullOrWhiteSpace(email) ? "alice@example.com" : email.Trim();
        var effectivePage = Math.Max(page ?? 1, 0);
        var effectiveSize = Math.Clamp(size ?? 2, 1, 50);

        var examples = new List<LinqExampleResult>();

        var whereResult = await db.Users
            .Where(u => u.IsActive)
            .Select(u => new { u.Id, u.Email, u.IsActive })
            .ToListAsync(ct);
        examples.Add(new LinqExampleResult(
            "Where",
            "Filter rows in SQL",
            "db.Users.Where(u => u.IsActive)",
            whereResult));

        var selectResult = await db.Users
            .Select(u => new UserDto
            {
                Id = u.Id,
                Name = u.FirstName + " " + u.LastName
            })
            .ToListAsync(ct);
        examples.Add(new LinqExampleResult(
            "Select",
            "Project to DTO",
            "db.Users.Select(u => new UserDto { Id = u.Id, Name = u.FirstName + \" \" + u.LastName })",
            selectResult));

        var selectManyResult = await db.Users
            .SelectMany(u => u.Roles)
            .Select(r => new
            {
                User = r.User.Email,
                Role = r.Name
            })
            .ToListAsync(ct);
        examples.Add(new LinqExampleResult(
            "SelectMany",
            "Flatten 1->N navigation",
            "db.Users.SelectMany(u => u.Roles)",
            selectManyResult));

        var orderByResult = await db.Users
            .OrderBy(u => u.CreatedAt)
            .Select(u => new { u.Email, u.CreatedAt })
            .ToListAsync(ct);
        examples.Add(new LinqExampleResult(
            "OrderBy",
            "Sort ascending",
            "db.Users.OrderBy(u => u.CreatedAt)",
            orderByResult));

        var orderByDescResult = await db.Posts
            .OrderByDescending(p => p.Id)
            .Select(p => new { p.Id, p.Title, p.UserId })
            .ToListAsync(ct);
        examples.Add(new LinqExampleResult(
            "OrderByDescending",
            "Sort descending",
            "db.Posts.OrderByDescending(p => p.Id)",
            orderByDescResult));

        var thenByResult = await db.Users
            .OrderBy(u => u.LastName)
            .ThenBy(u => u.FirstName)
            .Select(u => new { u.LastName, u.FirstName, u.Email })
            .ToListAsync(ct);
        examples.Add(new LinqExampleResult(
            "ThenBy",
            "Secondary sort key",
            "db.Users.OrderBy(u => u.LastName).ThenBy(u => u.FirstName)",
            thenByResult));

        var groupByResult = await db.Orders
            .GroupBy(o => o.UserId)
            .Select(g => new
            {
                g.Key,
                Count = g.Count(),
                Total = g.Sum(o => o.Total)
            })
            .OrderBy(g => g.Key)
            .ToListAsync(ct);
        examples.Add(new LinqExampleResult(
            "GroupBy",
            "Reporting and aggregations",
            "db.Orders.GroupBy(o => o.UserId).Select(g => new { g.Key, Count = g.Count() })",
            groupByResult));

        var joinResult = await db.Orders
            .Join(
                db.Users,
                o => o.UserId,
                u => u.Id,
                (o, u) => new
                {
                    o.Id,
                    Name = u.FirstName + " " + u.LastName,
                    o.Total
                })
            .OrderBy(x => x.Id)
            .ToListAsync(ct);
        examples.Add(new LinqExampleResult(
            "Join",
            "Manual join when no navigation is used",
            "db.Orders.Join(db.Users, o => o.UserId, u => u.Id, (o, u) => new { o.Id, u.Name })",
            joinResult));

        var anyResult = await db.Users.AnyAsync(u => u.Email == effectiveEmail, ct);
        examples.Add(new LinqExampleResult(
            "Any",
            "EXISTS check in SQL",
            "db.Users.Any(u => u.Email == email)",
            new { email = effectiveEmail, exists = anyResult }));

        var allResult = await db.Items.AllAsync(i => i.IsValid, ct);
        examples.Add(new LinqExampleResult(
            "All",
            "Validate every row",
            "db.Items.All(i => i.IsValid)",
            new { allValid = allResult }));

        var firstOrDefaultResult = await db.Users
            .Where(u => u.IsActive)
            .OrderBy(u => u.Id)
            .Select(u => new { u.Id, u.Email })
            .FirstOrDefaultAsync(ct);
        examples.Add(new LinqExampleResult(
            "FirstOrDefault",
            "Read one row safely",
            "db.Users.FirstOrDefault(u => u.Id == id)",
            firstOrDefaultResult));

        var singleOrDefaultResult = await db.Users
            .SingleOrDefaultAsync(u => u.Email == effectiveEmail, ct);
        examples.Add(new LinqExampleResult(
            "SingleOrDefault",
            "Expect uniqueness",
            "db.Users.SingleOrDefault(u => u.Email == email)",
            singleOrDefaultResult is null
                ? null
                : new
                {
                    singleOrDefaultResult.Id,
                    singleOrDefaultResult.Email,
                    singleOrDefaultResult.IsActive
                }));

        var countResult = await db.Users.CountAsync(u => u.IsActive, ct);
        examples.Add(new LinqExampleResult(
            "Count",
            "COUNT aggregate",
            "db.Users.Count(u => u.IsActive)",
            new { activeCount = countResult }));

        var skipResult = await db.Posts
            .OrderByDescending(p => p.Id)
            .Skip(effectiveSize)
            .Select(p => new { p.Id, p.Title })
            .ToListAsync(ct);
        examples.Add(new LinqExampleResult(
            "Skip",
            "Skip rows for paging",
            "db.Posts.OrderByDescending(p => p.Id).Skip(pageSize)",
            skipResult));

        var takeResult = await db.Posts
            .OrderByDescending(p => p.Id)
            .Skip(effectivePage * effectiveSize)
            .Take(effectiveSize)
            .Select(p => new { p.Id, p.Title })
            .ToListAsync(ct);
        examples.Add(new LinqExampleResult(
            "Take",
            "Take page chunk",
            "db.Posts.Skip(page * size).Take(size)",
            takeResult));

        var sumResult = await db.Payments.SumAsync(p => p.Amount, ct);
        examples.Add(new LinqExampleResult(
            "Sum",
            "Numeric aggregate",
            "db.Payments.Sum(p => p.Amount)",
            new { totalAmount = sumResult }));

        var minResult = await db.Orders.MinAsync(o => o.Total, ct);
        var maxResult = await db.Orders.MaxAsync(o => o.Total, ct);
        examples.Add(new LinqExampleResult(
            "Min/Max",
            "Range boundaries",
            "db.Orders.Min(o => o.Total) / db.Orders.Max(o => o.Total)",
            new { min = minResult, max = maxResult }));

        var includeUsers = await db.Users
            .Include(u => u.Posts)
            .OrderBy(u => u.Id)
            .ToListAsync(ct);
        var includeResult = includeUsers.Select(u => new
        {
            u.Id,
            u.Email,
            PostCount = u.Posts.Count,
            Posts = u.Posts.OrderBy(p => p.Id).Select(p => p.Title).ToList()
        }).ToList();
        examples.Add(new LinqExampleResult(
            "Include",
            "Eager load navigation",
            "db.Users.Include(u => u.Posts)",
            includeResult));

        var toListResult = (await db.Users
                .Where(u => u.IsActive)
                .ToListAsync(ct))
            .Select(u => new { u.Id, u.Email })
            .ToList();
        examples.Add(new LinqExampleResult(
            "ToList",
            "Execute query immediately",
            "var list = db.Users.Where(u => u.IsActive).ToList()",
            toListResult));

        var dbPath = db.Database.GetDbConnection().DataSource;
        return new LinqExamplesResponse(
            DateTime.UtcNow,
            dbPath,
            effectiveEmail,
            effectivePage,
            effectiveSize,
            examples);
    }
}
