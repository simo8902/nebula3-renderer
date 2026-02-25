using Microsoft.EntityFrameworkCore;

namespace NDEVC.Web.Api.LinqDemo;

public static class LinqDemoSeeder
{
    public static async Task EnsureSeededAsync(LinqDemoDbContext db, CancellationToken ct = default)
    {
        await db.Database.EnsureCreatedAsync(ct);
        if (await db.Users.AnyAsync(ct))
        {
            return;
        }

        await SeedDataAsync(db, ct);
    }

    public static async Task ResetAsync(LinqDemoDbContext db, CancellationToken ct = default)
    {
        await db.Database.EnsureDeletedAsync(ct);
        await db.Database.EnsureCreatedAsync(ct);
        await SeedDataAsync(db, ct);
    }

    private static async Task SeedDataAsync(LinqDemoDbContext db, CancellationToken ct)
    {
        var alice = new DemoUser
        {
            FirstName = "Alice",
            LastName = "Stone",
            Email = "alice@example.com",
            IsActive = true,
            CreatedAt = new DateTime(2025, 1, 10, 8, 0, 0, DateTimeKind.Utc),
            Roles =
            {
                new DemoRole { Name = "Admin" },
                new DemoRole { Name = "Editor" }
            },
            Posts =
            {
                new DemoPost { Title = "EF Core Tips for Runtime Data", CreatedAt = new DateTime(2025, 1, 11, 9, 0, 0, DateTimeKind.Utc) },
                new DemoPost { Title = "Indexing Notes", CreatedAt = new DateTime(2025, 1, 14, 15, 30, 0, DateTimeKind.Utc) }
            },
            Orders =
            {
                new DemoOrder { Total = 120.50m, CreatedAt = new DateTime(2025, 1, 12, 10, 0, 0, DateTimeKind.Utc) },
                new DemoOrder { Total = 35.00m, CreatedAt = new DateTime(2025, 1, 16, 10, 0, 0, DateTimeKind.Utc) }
            },
            Payments =
            {
                new DemoPayment { Amount = 40.00m, PaidAt = new DateTime(2025, 1, 12, 10, 5, 0, DateTimeKind.Utc) },
                new DemoPayment { Amount = 60.00m, PaidAt = new DateTime(2025, 1, 16, 10, 10, 0, DateTimeKind.Utc) }
            }
        };

        var bob = new DemoUser
        {
            FirstName = "Bob",
            LastName = "Young",
            Email = "bob@example.com",
            IsActive = false,
            CreatedAt = new DateTime(2025, 1, 3, 8, 0, 0, DateTimeKind.Utc),
            Roles =
            {
                new DemoRole { Name = "Viewer" }
            },
            Posts =
            {
                new DemoPost { Title = "Legacy Pipeline Draft", CreatedAt = new DateTime(2025, 1, 6, 12, 0, 0, DateTimeKind.Utc) }
            },
            Orders =
            {
                new DemoOrder { Total = 15.00m, CreatedAt = new DateTime(2025, 1, 7, 9, 0, 0, DateTimeKind.Utc) }
            },
            Payments =
            {
                new DemoPayment { Amount = 15.00m, PaidAt = new DateTime(2025, 1, 7, 9, 15, 0, DateTimeKind.Utc) }
            }
        };

        var charlie = new DemoUser
        {
            FirstName = "Charlie",
            LastName = "Adams",
            Email = "charlie@example.com",
            IsActive = true,
            CreatedAt = new DateTime(2025, 1, 15, 8, 0, 0, DateTimeKind.Utc),
            Roles =
            {
                new DemoRole { Name = "Support" },
                new DemoRole { Name = "Editor" }
            },
            Posts =
            {
                new DemoPost { Title = "Query Benchmarks", CreatedAt = new DateTime(2025, 1, 17, 8, 0, 0, DateTimeKind.Utc) },
                new DemoPost { Title = "CDN Diagnostics", CreatedAt = new DateTime(2025, 1, 18, 8, 30, 0, DateTimeKind.Utc) }
            },
            Orders =
            {
                new DemoOrder { Total = 300.00m, CreatedAt = new DateTime(2025, 1, 18, 11, 0, 0, DateTimeKind.Utc) }
            },
            Payments =
            {
                new DemoPayment { Amount = 90.00m, PaidAt = new DateTime(2025, 1, 18, 11, 10, 0, DateTimeKind.Utc) }
            }
        };

        var diana = new DemoUser
        {
            FirstName = "Diana",
            LastName = "Adams",
            Email = "diana@example.com",
            IsActive = true,
            CreatedAt = new DateTime(2025, 1, 12, 8, 0, 0, DateTimeKind.Utc),
            Roles =
            {
                new DemoRole { Name = "Viewer" }
            },
            Posts =
            {
                new DemoPost { Title = "Streaming Hub Overview", CreatedAt = new DateTime(2025, 1, 13, 8, 0, 0, DateTimeKind.Utc) },
                new DemoPost { Title = "SQLite Snapshot Notes", CreatedAt = new DateTime(2025, 1, 20, 8, 45, 0, DateTimeKind.Utc) }
            },
            Orders =
            {
                new DemoOrder { Total = 225.00m, CreatedAt = new DateTime(2025, 1, 20, 12, 0, 0, DateTimeKind.Utc) }
            },
            Payments =
            {
                new DemoPayment { Amount = 110.00m, PaidAt = new DateTime(2025, 1, 20, 12, 10, 0, DateTimeKind.Utc) }
            }
        };

        db.Users.AddRange(alice, bob, charlie, diana);
        db.Items.AddRange(
            new DemoItem { Sku = "CPU-001", IsValid = true },
            new DemoItem { Sku = "GPU-002", IsValid = true },
            new DemoItem { Sku = "SSD-003", IsValid = true });

        await db.SaveChangesAsync(ct);
    }
}
