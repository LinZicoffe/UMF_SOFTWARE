using System.Windows;
using System.Windows.Threading;
using Microsoft.Extensions.DependencyInjection;
using Serilog;
using UMF流量计.Communication;
using UMF流量计.Services;
using UMF流量计.ViewModels;

namespace UMF流量计;

public partial class App : Application
{
    public IServiceProvider Services { get; private set; } = null!;

    private void OnStartup(object sender, StartupEventArgs e)
    {
        // 全局异常处理：屏蔽所有未处理异常（包括通信超时等）
        DispatcherUnhandledException += (s, args) =>
        {
            Log.Debug(args.Exception, "全局未处理异常已捕获");
            args.Handled = true;
        };
        AppDomain.CurrentDomain.UnhandledException += (s, args) =>
        {
            if (args.ExceptionObject is Exception ex)
                Log.Debug(ex, "AppDomain 未处理异常");
        };
        TaskScheduler.UnobservedTaskException += (s, args) =>
        {
            Log.Debug(args.Exception, "Task 未观察异常");
            args.SetObserved();
        };

        Log.Logger = new LoggerConfiguration()
            .MinimumLevel.Information()
            .WriteTo.File("logs\\umf-.log", rollingInterval: RollingInterval.Day,
                outputTemplate: "{Timestamp:yyyy-MM-dd HH:mm:ss.fff zzz} [{Level:u3}] {Message:lj}{NewLine}{Exception}")
            .CreateLogger();

        Log.Information("UMF 流量监控系统启动");

        var services = new ServiceCollection();
        ConfigureServices(services);
        Services = services.BuildServiceProvider();

        var mainWindow = new MainWindow
        {
            DataContext = Services.GetRequiredService<MainViewModel>()
        };
        mainWindow.Show();
    }

    private void ConfigureServices(IServiceCollection services)
    {
        services.AddSingleton<IModbusService, ModbusRtuService>();
        services.AddSingleton<IDataService, DataService>();
        services.AddSingleton<MainViewModel>();
    }

    protected override async void OnExit(ExitEventArgs e)
    {
        if (Services is ServiceProvider sp)
        {
            var dataService = sp.GetService<IDataService>();
            if (dataService != null)
                await dataService.DisposeAsync();

            var modbusService = sp.GetService<IModbusService>();
            if (modbusService != null)
                await modbusService.DisposeAsync();
        }

        Log.CloseAndFlush();
        base.OnExit(e);
    }
}
