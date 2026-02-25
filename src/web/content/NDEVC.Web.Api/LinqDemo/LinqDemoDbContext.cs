using Microsoft.EntityFrameworkCore;

namespace NDEVC.Web.Api.LinqDemo;

public sealed class LinqDemoDbContext(DbContextOptions<LinqDemoDbContext> options) : DbContext(options)
{
    public DbSet<DemoUser> Users => Set<DemoUser>();
    public DbSet<DemoRole> Roles => Set<DemoRole>();
    public DbSet<DemoPost> Posts => Set<DemoPost>();
    public DbSet<DemoOrder> Orders => Set<DemoOrder>();
    public DbSet<DemoPayment> Payments => Set<DemoPayment>();
    public DbSet<DemoItem> Items => Set<DemoItem>();

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<DemoUser>(entity =>
        {
            entity.HasIndex(u => u.Email).IsUnique();
            entity.Property(u => u.FirstName).HasMaxLength(64);
            entity.Property(u => u.LastName).HasMaxLength(64);
            entity.Property(u => u.Email).HasMaxLength(255);
        });

        modelBuilder.Entity<DemoRole>(entity =>
        {
            entity.Property(r => r.Name).HasMaxLength(64);
            entity.HasOne(r => r.User)
                .WithMany(u => u.Roles)
                .HasForeignKey(r => r.UserId);
        });

        modelBuilder.Entity<DemoPost>(entity =>
        {
            entity.Property(p => p.Title).HasMaxLength(256);
            entity.HasOne(p => p.User)
                .WithMany(u => u.Posts)
                .HasForeignKey(p => p.UserId);
        });

        modelBuilder.Entity<DemoOrder>(entity =>
        {
            entity.Property(o => o.Total).HasPrecision(18, 2);
            entity.HasOne(o => o.User)
                .WithMany(u => u.Orders)
                .HasForeignKey(o => o.UserId);
        });

        modelBuilder.Entity<DemoPayment>(entity =>
        {
            entity.Property(p => p.Amount).HasPrecision(18, 2);
            entity.HasOne(p => p.User)
                .WithMany(u => u.Payments)
                .HasForeignKey(p => p.UserId);
        });

        modelBuilder.Entity<DemoItem>(entity =>
        {
            entity.Property(i => i.Sku).HasMaxLength(64);
        });
    }
}
