using CommunityToolkit.Mvvm.ComponentModel;
using UMF流量计.Models;
using UMF流量计.Services;

namespace UMF流量计.ViewModels;

public partial class DashboardViewModel : ObservableObject
{
    private readonly IDataService _dataService;

    [ObservableProperty]
    private float _flowRate;

    [ObservableProperty]
    private float _temperature;

    [ObservableProperty]
    private float _pressure;

    [ObservableProperty]
    private ulong _cumulativeFlow;

    [ObservableProperty]
    private DateTime _lastUpdateTime;

    [ObservableProperty]
    private bool _isOnline;

    public DashboardViewModel(IDataService dataService)
    {
        _dataService = dataService;
        _dataService.DataUpdated += OnDataUpdated;
        _dataService.CumulativeFlowUpdated += OnCumulativeUpdated;
        _dataService.ConnectionStateChanged += OnConnectionChanged;
    }

    private void OnDataUpdated(object? sender, SensorData data)
    {
        App.Current.Dispatcher.Invoke(() =>
        {
            FlowRate = data.FlowRate;
            Temperature = data.Temperature;
            Pressure = data.Pressure;
            LastUpdateTime = data.Timestamp;
        });
    }

    private void OnCumulativeUpdated(object? sender, ulong cumulative)
    {
        App.Current.Dispatcher.Invoke(() => CumulativeFlow = cumulative);
    }

    private void OnConnectionChanged(object? sender, bool connected)
    {
        App.Current.Dispatcher.Invoke(() => IsOnline = connected);
    }
}
