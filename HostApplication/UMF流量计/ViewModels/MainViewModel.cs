using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using UMF流量计.Communication;
using UMF流量计.Models;
using UMF流量计.Services;

namespace UMF流量计.ViewModels;

public class MenuItemModel
{
    public string Icon { get; set; } = "";
    public string Title { get; set; } = "";
}

public partial class MainViewModel : ObservableObject
{
    private readonly IModbusService _modbusService;
    private readonly IDataService _dataService;

    [ObservableProperty]
    private string _title = "UMF 超声波流量监控系统";

    [ObservableProperty]
    private int _selectedNavigationIndex;

    [ObservableProperty]
    private bool _isConnected;

    [ObservableProperty]
    private string _statusText = "未连接";

    [ObservableProperty]
    private string _connectionInfo = "未连接";

    [ObservableProperty]
    private SensorData? _latestSensorData;

    [ObservableProperty]
    private ulong _cumulativeFlow;

    public List<MenuItemModel> MenuItems { get; } = new()
    {
        new MenuItemModel { Icon = "\U0001F517", Title = "连接设置" },
        new MenuItemModel { Icon = "\U0001F4CA", Title = "实时监控" },
        new MenuItemModel { Icon = "\U0001F4C8", Title = "趋势图表" },
        new MenuItemModel { Icon = "\U0001F527", Title = "DAC 校准" },
        new MenuItemModel { Icon = "\u2699", Title = "参数配置" },
        new MenuItemModel { Icon = "\U0001F3AE", Title = "运行控制" },
        new MenuItemModel { Icon = "\U0001F9EA", Title = "模拟控制" },
    };

    public ConnectionViewModel ConnectionVm { get; }
    public DashboardViewModel DashboardVm { get; }
    public CalibrationViewModel CalibrationVm { get; }
    public ConfigViewModel ConfigVm { get; }
    public ControlViewModel ControlVm { get; }
    public SimulationViewModel SimulationVm { get; }
    public TrendViewModel TrendVm { get; }

    public MainViewModel(IModbusService modbusService, IDataService dataService)
    {
        _modbusService = modbusService;
        _dataService = dataService;

        ConnectionVm = new ConnectionViewModel(_modbusService, _dataService);
        DashboardVm = new DashboardViewModel(_dataService);
        CalibrationVm = new CalibrationViewModel(_modbusService);
        ConfigVm = new ConfigViewModel(_modbusService);
        ControlVm = new ControlViewModel(_modbusService);
        SimulationVm = new SimulationViewModel(_modbusService);
        TrendVm = new TrendViewModel(_dataService);

        _dataService.DataUpdated += OnDataUpdated;
        _dataService.CumulativeFlowUpdated += OnCumulativeFlowUpdated;
        _dataService.ConnectionStateChanged += OnConnectionStateChanged;
        _dataService.ConnectionLost += OnConnectionLost;

        _modbusService.ConnectionStateChanged += (s, connected) =>
        {
            App.Current.Dispatcher.Invoke(() =>
            {
                IsConnected = connected;
                UpdateStatusText();
            });
        };
    }

    private void OnDataUpdated(object? sender, SensorData data)
    {
        App.Current.Dispatcher.Invoke(() =>
        {
            LatestSensorData = data;
            StatusText = $"已连接 | 流量: {data.FlowRate:F2} m³/h | 温度: {data.Temperature:F1} °C";
        });
    }

    private void OnCumulativeFlowUpdated(object? sender, ulong cumulative)
    {
        App.Current.Dispatcher.Invoke(() => CumulativeFlow = cumulative);
    }

    private void OnConnectionStateChanged(object? sender, bool connected)
    {
        App.Current.Dispatcher.Invoke(() =>
        {
            IsConnected = connected;
            UpdateStatusText();
        });
    }

    private void OnConnectionLost(object? sender, Exception ex)
    {
        App.Current.Dispatcher.Invoke(() =>
        {
            StatusText = $"连接丢失: {ex.Message}";
        });
    }

    private void UpdateStatusText()
    {
        ConnectionInfo = IsConnected
            ? $"{ConnectionVm.SelectedPort} @ {ConnectionVm.SelectedBaudRate} | 从站: {ConnectionVm.SlaveAddress} | 采集: {ConnectionVm.PollInterval}ms | 已连接"
            : "未连接";
    }
}
