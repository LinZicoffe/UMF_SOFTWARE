using System.Collections.Concurrent;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using UMF流量计.Models;
using UMF流量计.Services;

namespace UMF流量计.ViewModels;

public partial class TrendViewModel : ObservableObject
{
    private readonly IDataService _dataService;
    private readonly ConcurrentQueue<SensorData> _historyData = new();
    private const int MaxDataPoints = 3600;

    [ObservableProperty]
    private List<TrendPoint> _flowRatePoints = new();

    [ObservableProperty]
    private List<TrendPoint> _temperaturePoints = new();

    [ObservableProperty]
    private List<TrendPoint> _pressurePoints = new();

    [ObservableProperty]
    private bool _isPaused;

    public TrendViewModel(IDataService dataService)
    {
        _dataService = dataService;
        _dataService.DataUpdated += OnDataUpdated;
    }

    private void OnDataUpdated(object? sender, SensorData data)
    {
        if (IsPaused) return;

            _historyData.Enqueue(data);
            while (_historyData.Count > MaxDataPoints)
                _historyData.TryDequeue(out _);

        App.Current.Dispatcher.Invoke(() =>
        {
            var points = _historyData.ToList();
            FlowRatePoints = points.Select(p => new TrendPoint(p.Timestamp, p.FlowRate)).ToList();
            TemperaturePoints = points.Select(p => new TrendPoint(p.Timestamp, p.Temperature)).ToList();
            PressurePoints = points.Select(p => new TrendPoint(p.Timestamp, p.Pressure)).ToList();
        });
    }

    [RelayCommand]
    private void TogglePause()
    {
        IsPaused = !IsPaused;
    }

    [RelayCommand]
    private void ClearData()
    {
        while (_historyData.TryDequeue(out _)) { }
        FlowRatePoints = new List<TrendPoint>();
        TemperaturePoints = new List<TrendPoint>();
        PressurePoints = new List<TrendPoint>();
    }
}

public record TrendPoint(DateTime Time, double Value);
