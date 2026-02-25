namespace NDEVC.Web.Api.LinqDemo;

public sealed class DemoUser
{
    public int Id { get; set; }
    public string FirstName { get; set; } = string.Empty;
    public string LastName { get; set; } = string.Empty;
    public string Email { get; set; } = string.Empty;
    public bool IsActive { get; set; }
    public DateTime CreatedAt { get; set; }
    public List<DemoRole> Roles { get; } = new();
    public List<DemoPost> Posts { get; } = new();
    public List<DemoOrder> Orders { get; } = new();
    public List<DemoPayment> Payments { get; } = new();
}

public sealed class DemoRole
{
    public int Id { get; set; }
    public int UserId { get; set; }
    public string Name { get; set; } = string.Empty;
    public DemoUser User { get; set; } = null!;
}

public sealed class DemoPost
{
    public int Id { get; set; }
    public int UserId { get; set; }
    public string Title { get; set; } = string.Empty;
    public DateTime CreatedAt { get; set; }
    public DemoUser User { get; set; } = null!;
}

public sealed class DemoOrder
{
    public int Id { get; set; }
    public int UserId { get; set; }
    public decimal Total { get; set; }
    public DateTime CreatedAt { get; set; }
    public DemoUser User { get; set; } = null!;
}

public sealed class DemoPayment
{
    public int Id { get; set; }
    public int UserId { get; set; }
    public decimal Amount { get; set; }
    public DateTime PaidAt { get; set; }
    public DemoUser User { get; set; } = null!;
}

public sealed class DemoItem
{
    public int Id { get; set; }
    public string Sku { get; set; } = string.Empty;
    public bool IsValid { get; set; }
}
